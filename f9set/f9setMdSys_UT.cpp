// \file f9set/f9setMdSys_UT.cpp
//
// SET 行情系統整合測試: 透過 plugin 建立 ExgMdSystem, 用本機的假伺服器驗證:
// - Mc:      UDP 收到 MoldUDP64 封包;
// - Glimpse: 盤中啟動, 連線取得快照, 期間的即時封包先暫存, 快照完成後依序處理, 然後斷線;
// - Rr:      缺號時送出回補要求, 回補封包補上缺號;
// - 回補失敗: 放棄缺號後, 重新連線 GLIMPSE 取得快照;
// - PkLog:   錄製的封包格式, 可用 f9setExgMdItch_UT 重播.
//
#include "f9set/ExgMdSystem.hpp"
#include "f9set/ExgMdTestPk.hpp"
#include "fon9/seed/PluginsMgr.hpp"
#include "fon9/framework/IoManagerTree.hpp"
#include "fon9/DefaultThreadPool.hpp"
#include "fon9/TestTools.hpp"

#include <thread>
#include <atomic>
#include <mutex>
#include <vector>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

extern "C" fon9_API  fon9::seed::PluginsDesc f9p_Dgram;
extern "C" fon9_API  fon9::seed::PluginsDesc f9p_TcpClient;
extern "C" f9set_API fon9::seed::PluginsDesc f9p_SetMdSys;
void* ForceLinkSomething() {
   static const void* forceLinkList[]{&f9p_Dgram, &f9p_TcpClient, &f9p_SetMdSys};
   return forceLinkList;
}

static const char     kSession[] = "0000000001";
static const uint32_t kPTT = 1001;

static sockaddr_in MakeAddr(uint16_t port) {
   sockaddr_in addr;
   memset(&addr, 0, sizeof(addr));
   addr.sin_family = AF_INET;
   addr.sin_port = htons(port);
   addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
   return addr;
}
static bool WaitFor(std::function<bool()> cond, unsigned ms = 5000) {
   for (unsigned L = 0; L < ms / 10; ++L) {
      if (cond())
         return true;
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
   }
   return cond();
}
//--------------------------------------------------------------------------//
/// 假的 Re-request server: 收到 Request Packet 後, 若 Replies_ 有對應的封包, 就回覆.
struct FakeRrServer {
   int                        Fd_;
   std::thread                Thread_;
   std::atomic<bool>          IsQuit_{false};
   std::mutex                 Mutex_;
   std::vector<f9set::MoldUdp64Request>  Requests_;
   std::vector<std::string>   Replies_;

   FakeRrServer(uint16_t port) {
      this->Fd_ = socket(AF_INET, SOCK_DGRAM, 0);
      sockaddr_in addr = MakeAddr(port);
      if (bind(this->Fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0)
         perror("FakeRrServer.bind");
      this->Thread_ = std::thread(&FakeRrServer::Run, this);
   }
   ~FakeRrServer() {
      this->IsQuit_ = true;
      this->Thread_.join();
      close(this->Fd_);
   }
   size_t RequestCount() {
      std::lock_guard<std::mutex> lk{this->Mutex_};
      return this->Requests_.size();
   }
   void Run() {
      while (!this->IsQuit_) {
         pollfd pfd{this->Fd_, POLLIN, 0};
         if (poll(&pfd, 1, 20) <= 0)
            continue;
         f9set::MoldUdp64Request req;
         sockaddr_in             from;
         socklen_t               fromlen = sizeof(from);
         if (recvfrom(this->Fd_, &req, sizeof(req), 0, reinterpret_cast<sockaddr*>(&from), &fromlen) != sizeof(req))
            continue;
         std::lock_guard<std::mutex> lk{this->Mutex_};
         this->Requests_.push_back(req);
         for (const std::string& pk : this->Replies_) {
            if (reinterpret_cast<const f9set::MoldUdp64Head*>(pk.data())->SeqNo_.Get() == req.SeqNo_.Get())
               sendto(this->Fd_, pk.data(), pk.size(), 0, reinterpret_cast<sockaddr*>(&from), fromlen);
         }
      }
   }
};
//--------------------------------------------------------------------------//
/// 假的 GLIMPSE server: 收到 Login 之後, 送出 LoginAccepted + Snapshot_, 然後等對方斷線.
struct FakeGlimpseServer {
   int               Fd_;
   std::thread       Thread_;
   std::atomic<bool> IsQuit_{false};
   std::mutex        Mutex_;
   std::string       Snapshot_;
   std::string       LastLogin_;
   std::atomic<unsigned> LoginCount_{0};
   std::atomic<unsigned> LogoutCount_{0};
   std::atomic<unsigned> ClosedCount_{0};
   /// 收到 Login 之後, 等候 Release_ 才送出快照, 讓測試可以在快照期間送出即時封包.
   std::atomic<bool> Release_{false};

   FakeGlimpseServer(uint16_t port) {
      this->Fd_ = socket(AF_INET, SOCK_STREAM, 0);
      int on = 1;
      setsockopt(this->Fd_, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
      sockaddr_in addr = MakeAddr(port);
      if (bind(this->Fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0)
         perror("FakeGlimpseServer.bind");
      listen(this->Fd_, 5);
      this->Thread_ = std::thread(&FakeGlimpseServer::Run, this);
   }
   ~FakeGlimpseServer() {
      this->IsQuit_ = true;
      this->Thread_.join();
      close(this->Fd_);
   }
   void SetSnapshot(const std::string& snapshot) {
      std::lock_guard<std::mutex> lk{this->Mutex_};
      this->Snapshot_ = snapshot;
   }
   void Run() {
      while (!this->IsQuit_) {
         pollfd pfd{this->Fd_, POLLIN, 0};
         if (poll(&pfd, 1, 20) <= 0)
            continue;
         int cli = accept(this->Fd_, nullptr, nullptr);
         if (cli < 0)
            continue;
         this->Serve(cli);
         close(cli);
         ++this->ClosedCount_;
      }
   }
   void Serve(int cli) {
      std::string rx;
      bool        isSent = false;
      while (!this->IsQuit_) {
         if (!isSent && this->LoginCount_ > this->ClosedCount_ && this->Release_) {
            std::string tx = MakeSoup('A', "0000000001                   1", 30);
            {
               std::lock_guard<std::mutex> lk{this->Mutex_};
               tx.append(this->Snapshot_);
            }
            send(cli, tx.data(), tx.size(), 0);
            isSent = true;
         }
         pollfd pfd{cli, POLLIN, 0};
         if (poll(&pfd, 1, 10) <= 0)
            continue;
         char buf[1024];
         ssize_t rsz = recv(cli, buf, sizeof(buf), 0);
         if (rsz <= 0)
            return;
         rx.append(buf, static_cast<size_t>(rsz));
         while (rx.size() >= 2) {
            const size_t pksz = 2u + fon9::GetBigEndian<uint16_t>(rx.data());
            if (rx.size() < pksz)
               break;
            switch (rx[2]) {
            case 'L':
               this->LastLogin_ = rx.substr(0, pksz);
               ++this->LoginCount_;
               break;
            case 'O':
               ++this->LogoutCount_;
               break;
            }
            rx.erase(0, pksz);
         }
      }
   }
};
//--------------------------------------------------------------------------//
struct McSender {
   int         Fd_;
   sockaddr_in Addr_;
   std::vector<std::string> Sent_;
   McSender(uint16_t port) : Fd_{socket(AF_INET, SOCK_DGRAM, 0)}, Addr_(MakeAddr(port)) {
   }
   ~McSender() {
      close(this->Fd_);
   }
   void Send(const std::string& pk) {
      this->Sent_.push_back(pk);
      sendto(this->Fd_, pk.data(), pk.size(), 0, reinterpret_cast<const sockaddr*>(&this->Addr_), sizeof(this->Addr_));
   }
};
//--------------------------------------------------------------------------//
static fon9::fmkt::Pri Pri2(int64_t v) {
   return fon9::fmkt::Pri::Make<2>(v);
}
static bool IsPQ(const fon9::fmkt::PriQty& pq, int64_t priV2, fon9::fmkt::Qty qty) {
   return pq.Pri_ == Pri2(priV2) && pq.Qty_ == qty;
}
static std::string MakeSnapshot(const std::vector<std::pair<char, std::pair<uint64_t, std::pair<f9set::ItchPrice, uint64_t>>>>& orders,
                                const char* nextSeqNo) {
   MoldPkBuilder snap{kSession, 1};
   snap.Seconds(1792029600); // 2026-10-15 09:00:00 (UTC+7).
   snap.Directory(kPTT, "PTT", 2, 100);
   snap.State(kPTT, "OPEN1");
   for (const auto& ord : orders)
      snap.AddOrder(kPTT, ord.first, ord.second.first, ord.second.second.first, ord.second.second.second);
   f9set::ItchEndOfSnapshot eos;
   eos.MsgType_ = 'G';
   eos.SeqNo_.AssignFrom(fon9::StrView_cstr(nextSeqNo));
   snap.AddRaw(&eos, sizeof(eos));
   return MoldToSoup(snap.Finish());
}
//--------------------------------------------------------------------------//
int main(int argc, char* argv[]) {
   (void)argc; (void)argv;
   ForceLinkSomething();
   fon9::AutoPrintTestInfo utinfo{"f9set MdSys"};
   // 在暫存目錄執行, 結束時移除(PkLog, Symbs.mds...).
   char tmpdir[] = "/tmp/f9setMdSys_UT.XXXXXX";
   if (mkdtemp(tmpdir) == nullptr || chdir(tmpdir) != 0) {
      perror("mkdtemp");
      return 3;
   }
   const uint16_t kPortBase = static_cast<uint16_t>(30000 + (getpid() % 10000) * 3);
   const uint16_t kMcPort = kPortBase, kRrPort = kPortBase + 1u, kGlimpsePort = kPortBase + 2u;
   FakeRrServer      rrServer{kRrPort};
   FakeGlimpseServer glimpseServer{kGlimpsePort};
   McSender          mc{kMcPort};

   fon9::seed::MaTreeSP root{new fon9::seed::MaTree{"f9setMdSys_UT"}};
   fon9::seed::PluginsMgrSP plugins{new fon9::seed::PluginsMgr(root, "Plugins")};
   root->Add(plugins, "Plant.Plugins");
   #define _ "\x01"
   plugins->LoadConfigStr(
      "Id"       _ "Enabled" _ "FileName" _ "EntryName" _ "Args\n"
      "DevDgram" _ "Y"       _ ""         _ "Dgram"     _ "Name=Dgram|AddTo=FpDevice\n"
      "DevTcpc"  _ "Y"       _ ""         _ "TcpClient" _ "Name=TcpClient|AddTo=FpDevice\n"
      "SetMd"    _ "Y"       _ ""         _ "SetMdSys"  _ "Name=SetMd|UseRtiForRecover=N|TimeZone=+7"
                                                          "|GapWait=0.02|RetryInterval=0.1|MaxRetry=2"
                                                          "|Io={SvcCfg='ThreadCount=1'|Cfg=SetMd_io.f9gv}\n"
   );
   f9set::ExgMdSystemSP mdsys = root->Get<f9set::ExgMdSystem>("SetMd");
   fon9_CheckTestResult("Plugin.SetMdSys", mdsys.get() != nullptr);
   if (!mdsys)
      return 3;
   auto iomgr = mdsys->Sapling_->GetSapling<fon9::IoManagerTree>("SetMd_io");
   fon9_CheckTestResult("Plugin.IoMgr", iomgr.get() != nullptr);
   if (!iomgr)
      return 3;

   // 盤中啟動: 快照的下一個即時序號 = 10.
   glimpseServer.SetSnapshot(MakeSnapshot({{'B', {1, {3400, 500}}}, {'S', {3, {3425, 200}}}}, "10"));
   const std::string ioCfg = fon9::RevPrintTo<std::string>(
      "Id"      _ "Enabled" _ "Sch" _ "Session"   _ "SessionArgs"                      _ "Device"    _ "DeviceArgs\n"
      "McA"     _ "Y"       _ ""    _ "SetMdRecv" _ "Type=Mc|PkLog=McA.bin"             _ "Dgram"     _ "Bind=127.0.0.1:", kMcPort, "\n"
      "Rr"      _ "Y"       _ ""    _ "SetMdRecv" _ "Type=Rr"                           _ "Dgram"     _ "127.0.0.1:", kRrPort, "\n"
      "Glimpse" _ "Y"       _ ""    _ "SetMdRecv" _ "Type=Glimpse|User=user01|Pass=pw" _ "TcpClient" _ "127.0.0.1:", kGlimpsePort, "\n");
   iomgr->LoadConfigStr(fon9::ToStrView(ioCfg), fon9::TimeInterval_Millisecond(10));

   auto getNextSeqNo = [&mdsys]() {
      return mdsys->LockBuilder([](f9set::ExgMdRtsBuilder& b) { return b.GetNextSeqNo(); });
   };
   auto isWaitingSnapshot = [&mdsys]() { return mdsys->IsWaitingSnapshot(); };
   std::cout << "--- GLIMPSE ---" << std::endl;
   fon9_CheckTestResult("Glimpse.Login", WaitFor([&]() { return glimpseServer.LoginCount_ == 1; })
                        && glimpseServer.LastLogin_.size() == 49
                        && glimpseServer.LastLogin_.compare(3, 16, "user01pw        ") == 0
                        && glimpseServer.LastLogin_.compare(29, 20, "                   1") == 0
                        && isWaitingSnapshot());
   // 快照期間的即時封包: 9 已包含在快照, 10,11 暫存.
   std::this_thread::sleep_for(std::chrono::milliseconds(50)); // 等 McA 開啟.
   MoldPkBuilder pk9{kSession, 9};
   pk9.AddOrder(kPTT, 'S', 3, 3425, 200);
   pk9.AddOrder(kPTT, 'S', 200, 3500, 100);    // 10
   pk9.Executed(kPTT, 'S', 3, 50, 1, 1000000); // 11: 34.25 x 50
   mc.Send(pk9.Finish());
   fon9_CheckTestResult("Glimpse.Pending", WaitFor([&]() {
      return mdsys->LockBuilder([](f9set::ExgMdRtsBuilder& b) { return b.GetPendingCount() == 3; });
   }));
   glimpseServer.Release_ = true;
   fon9_CheckTestResult("Glimpse.Done", WaitFor([&]() { return !isWaitingSnapshot() && getNextSeqNo() == 12; }));
   fon9_CheckTestResult("Glimpse.Logout", WaitFor([&]() { return glimpseServer.LogoutCount_ == 1 && glimpseServer.ClosedCount_ == 1; }));
   f9set::ExgMdSymbSP ptt = fon9::dynamic_pointer_cast<f9set::ExgMdSymb>(mdsys->Symbs_->GetSymb(fon9::StrView{"PTT"}));
   fon9_CheckTestResult("Symb.PTT", ptt.get() != nullptr);
   if (!ptt)
      return 3;
   auto checkSymb = [&](std::function<bool(const f9set::ExgMdSymb&)> fn) {
      return mdsys->LockBuilder([&](f9set::ExgMdRtsBuilder&) {
         auto lk = mdsys->Symbs_->SymbMap_.Lock();
         return fn(*ptt);
      });
   };
   fon9_CheckTestResult("Symb.AfterSnapshot", checkSymb([](const f9set::ExgMdSymb& symb) {
      const auto& bs = symb.BS_.Data_;
      return symb.ShUnit_ == 100 && symb.TradingSessionSt_ == f9fmkt_TradingSessionSt_Open
         && IsPQ(bs.Buys_[0], 3400, 500) && IsPQ(bs.Buys_[1], 0, 0)
         && IsPQ(bs.Sells_[0], 3425, 150) && IsPQ(bs.Sells_[1], 3500, 100)
         && symb.Deal_.Data_.TotalQty_ == 50 && symb.Deal_.Data_.Deal_.Pri_ == Pri2(3425)
         && symb.Deal_.Data_.DealTime() == fon9::TimeInterval_Hour(9) + fon9::TimeInterval_Millisecond(1);
   }));

   std::cout << "--- Re-request ---" << std::endl;
   {
      MoldPkBuilder rr{kSession, 12};
      rr.AddOrder(kPTT, 'B', 6, 3380, 100);
      rr.AddOrder(kPTT, 'B', 7, 3370, 100);
      std::lock_guard<std::mutex> lk{rrServer.Mutex_};
      rrServer.Replies_.push_back(rr.Finish());
   }
   MoldPkBuilder pk14{kSession, 14};
   pk14.AddOrder(kPTT, 'B', 5, 3390, 100);
   mc.Send(pk14.Finish());
   fon9_CheckTestResult("Rr.Recovered", WaitFor([&]() { return getNextSeqNo() == 15; }));
   fon9_CheckTestResult("Rr.Request", rrServer.RequestCount() == 1
                        && rrServer.Requests_[0].SeqNo_.Get() == 12 && rrServer.Requests_[0].MsgCount_.Get() == 2
                        && rrServer.Requests_[0].Session_ == fon9::StrView{kSession});
   fon9_CheckTestResult("Rr.NoLost", mdsys->LockBuilder([](f9set::ExgMdRtsBuilder& b) {
      return b.GapCount_ == 0 && b.RequestCount_ == 1 && b.GetPendingCount() == 0;
   }));
   fon9_CheckTestResult("Rr.BS", checkSymb([](const f9set::ExgMdSymb& symb) {
      const auto& bs = symb.BS_.Data_;
      return IsPQ(bs.Buys_[0], 3400, 500) && IsPQ(bs.Buys_[1], 3390, 100)
         && IsPQ(bs.Buys_[2], 3380, 100) && IsPQ(bs.Buys_[3], 3370, 100);
   }));

   std::cout << "--- Lost => GLIMPSE again ---" << std::endl;
   // [15,20) 回補伺服器不回覆, 重送 MaxRetry 次之後放棄, 然後重新取得快照(下一個序號=21).
   glimpseServer.Release_ = false;
   glimpseServer.SetSnapshot(MakeSnapshot({{'B', {1, {3400, 500}}}, {'B', {5, {3390, 100}}},
                                           {'B', {6, {3380, 100}}}, {'B', {8, {3360, 100}}},
                                           {'S', {3, {3425, 150}}}, {'S', {200, {3500, 100}}}}, "21"));
   MoldPkBuilder pk20{kSession, 20};
   pk20.AddOrder(kPTT, 'B', 8, 3360, 100);
   mc.Send(pk20.Finish());
   fon9_CheckTestResult("Lost", WaitFor([&]() {
      return mdsys->LockBuilder([](f9set::ExgMdRtsBuilder& b) { return b.GapCount_ == 1 && b.GapMsgCount_ == 5; });
   }) && rrServer.RequestCount() == 3);
   fon9_CheckTestResult("Lost.GlimpseAgain", WaitFor([&]() { return glimpseServer.LoginCount_ == 2 && isWaitingSnapshot(); }));
   MoldPkBuilder pk21{kSession, 21};
   pk21.AddOrder(kPTT, 'S', 9, 3525, 100);
   mc.Send(pk21.Finish());
   glimpseServer.Release_ = true;
   fon9_CheckTestResult("Lost.SnapshotDone", WaitFor([&]() { return !isWaitingSnapshot() && getNextSeqNo() == 22; }));
   fon9_CheckTestResult("Lost.BS", checkSymb([](const f9set::ExgMdSymb& symb) {
      const auto& bs = symb.BS_.Data_;
      return IsPQ(bs.Buys_[0], 3400, 500) && IsPQ(bs.Buys_[1], 3390, 100) && IsPQ(bs.Buys_[2], 3380, 100)
         && IsPQ(bs.Buys_[3], 3360, 100) && IsPQ(bs.Buys_[4], 0, 0)
         && IsPQ(bs.Sells_[0], 3425, 150) && IsPQ(bs.Sells_[1], 3500, 100) && IsPQ(bs.Sells_[2], 3525, 100);
   }));
   fon9_CheckTestResult("Lost.OrderNotFound", mdsys->LockBuilder([](f9set::ExgMdRtsBuilder& b) {
      return b.OrderNotFoundCount_ == 0;
   }));
   fon9_CheckTestResult("Glimpse.Closed", WaitFor([&]() { return glimpseServer.ClosedCount_ == 2; }));
   std::cout << mdsys->GetInfo() << std::endl;

   std::cout << "--- PkLog ---" << std::endl;
   // 只有 McA 收到的封包.
   const std::string pklogName = mdsys->LogPathPre() + "_McA.bin";
   bool isPkLogOK = WaitFor([&]() {
      FILE* fd = fopen(pklogName.c_str(), "rb");
      if (!fd)
         return false;
      std::string content;
      char        buf[4096];
      size_t      rsz;
      while ((rsz = fread(buf, 1, sizeof(buf), fd)) > 0)
         content.append(buf, rsz);
      fclose(fd);
      std::string expected;
      for (const std::string& pk : mc.Sent_) {
         char lenbuf[2];
         fon9::PutBigEndian(lenbuf, static_cast<uint16_t>(pk.size()));
         expected.append(lenbuf, 2);
         expected.append(pk);
      }
      return content == expected;
   });
   fon9_CheckTestResult("PkLog", isPkLogOK);

   // ----- 結束.
   fon9::GetDefaultTimerThread()->WaitForEndNow();
   root->OnParentSeedClear();
   fon9::WaitDefaultThreadPoolQuit(fon9::GetDefaultThreadPool());
   mdsys.reset();
   iomgr.reset();
   plugins.reset();
   ptt.reset();
   if (chdir("/") == 0)
      fon9_CheckTestResult("Cleanup", system((std::string{"rm -rf "} + tmpdir).c_str()) == 0);
}

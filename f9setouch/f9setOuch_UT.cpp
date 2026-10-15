// \file f9setouch/f9setOuch_UT.cpp
//
// SET OUCH 下單模組測試.
// - 訊息格式、SoupBinTCP Login、Order Token、手動下單指令解析、log 文字.
// - 整合測試: 透過 plugin "SetOuchTrade" 建立下單線路, 連到本機的假 OUCH 伺服器:
//   登入, 新單/改單/刪單/成交/拒絕, Heartbeat, 斷線重連(接續序號), 登入被拒, 無可用線路, Rx timeout, 流量管制.
//
#include "f9setouch/ExgOuchTrade.hpp"
#include "f9setouch/OuchFmtStr.hpp"
#include "fon9/seed/PluginsMgr.hpp"
#include "fon9/framework/IoManagerTree.hpp"
#include "fon9/DefaultThreadPool.hpp"
#include "fon9/TestTools.hpp"

#include <thread>
#include <atomic>
#include <map>
#include <mutex>
#include <vector>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

extern "C" fon9_API      fon9::seed::PluginsDesc f9p_TcpClient;
extern "C" f9setouch_API fon9::seed::PluginsDesc f9p_SetOuchTrade;
void* ForceLinkSomething() {
   static const void* forceLinkList[]{&f9p_TcpClient, &f9p_SetOuchTrade};
   return forceLinkList;
}

using namespace f9setouch;

static bool WaitFor(std::function<bool()> cond, unsigned ms = 5000) {
   for (unsigned L = 0; L < ms / 10; ++L) {
      if (cond())
         return true;
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
   }
   return cond();
}
template <class Msg>
static Msg MakeMsg(OuchMsgType type) {
   Msg msg;
   memset(&msg, ' ', sizeof(msg));
   msg.MsgType_ = static_cast<char>(type);
   return msg;
}
template <class Msg>
static std::string ToStr(const Msg& msg) {
   return std::string{reinterpret_cast<const char*>(&msg), sizeof(msg)};
}
static std::string ToText(const std::string& msg, bool isOutbound) {
   fon9::RevBufferList rbuf{256};
   if (isOutbound)
      RevPrintOuchOutbound(rbuf, msg.data(), static_cast<unsigned>(msg.size()));
   else
      RevPrintOuchInbound(rbuf, msg.data(), static_cast<unsigned>(msg.size()));
   return fon9::BufferTo<std::string>(rbuf.MoveOut());
}
static bool StartsWith(const std::string& str, const std::string& prefix) {
   return str.compare(0, prefix.size(), prefix) == 0;
}
static constexpr uint64_t kTimestampNs = 1792029600123456789ull; // 2026-10-15 02:00:00.123456789 UTC.

//--------------------------------------------------------------------------//

/// 假的 OUCH 伺服器(SoupBinTCP).
/// - 收到 Login 之後回覆 Login Accepted, 然後從要求的序號開始送出回報(Outbound_).
/// - 收到 Enter/Replace/Cancel 自動產生回報: Accepted(或 Qty >= 1000000 則 Rejected), Replaced, Cancelled.
/// - 每 100ms 送出 Server Heartbeat(Silent_ 時不送).
struct FakeOuchServer {
   struct Order {
      uint32_t OrderBookId_;
      char     Side_;
      uint64_t OrderId_;
      uint64_t Qty_;
      int32_t  Pri_;
   };
   int               Fd_;
   std::thread       Thread_;
   std::atomic<bool> IsQuit_{false};
   std::mutex        Mutex_;
   std::vector<std::string>   Outbound_;
   std::vector<std::string>   Inbound_;
   std::vector<std::string>   Logins_;
   std::map<std::string, Order> Orders_;
   uint64_t                   NextOrderId_{1000};
   std::atomic<unsigned>      HbCount_{0};
   std::atomic<unsigned>      ClosedCount_{0};
   std::atomic<bool>          Silent_{false};
   std::atomic<char>          RejectLogin_{0};
   std::atomic<bool>          DropNow_{false};

   FakeOuchServer(uint16_t port) {
      this->Fd_ = socket(AF_INET, SOCK_STREAM, 0);
      int on = 1;
      setsockopt(this->Fd_, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
      sockaddr_in addr;
      memset(&addr, 0, sizeof(addr));
      addr.sin_family = AF_INET;
      addr.sin_port = htons(port);
      addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
      if (bind(this->Fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0)
         perror("FakeOuchServer.bind");
      listen(this->Fd_, 5);
      this->Thread_ = std::thread(&FakeOuchServer::Run, this);
   }
   ~FakeOuchServer() {
      this->IsQuit_ = true;
      this->Thread_.join();
      close(this->Fd_);
   }
   size_t LoginCount() {
      std::lock_guard<std::mutex> lk{this->Mutex_};
      return this->Logins_.size();
   }
   size_t InboundCount() {
      std::lock_guard<std::mutex> lk{this->Mutex_};
      return this->Inbound_.size();
   }
   std::string Inbound(size_t idx) {
      std::lock_guard<std::mutex> lk{this->Mutex_};
      return idx < this->Inbound_.size() ? this->Inbound_[idx] : std::string{};
   }
   void PushOutbound(std::string msg) {
      std::lock_guard<std::mutex> lk{this->Mutex_};
      this->Outbound_.push_back(std::move(msg));
   }
   static std::string MakeSoup(char type, const std::string& payload) {
      std::string pk(3, type);
      fon9::PutBigEndian(&pk[0], static_cast<uint16_t>(payload.size() + 1));
      return pk + payload;
   }
   static void Send(int cli, const std::string& pk) {
      if (send(cli, pk.data(), pk.size(), MSG_NOSIGNAL) != static_cast<ssize_t>(pk.size()))
         perror("FakeOuchServer.send");
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
      bool        isLoggedIn = false;
      uint64_t    nextSend = 0;
      auto        lastHb = std::chrono::steady_clock::now();
      while (!this->IsQuit_) {
         if (this->DropNow_) {
            this->DropNow_ = false;
            return;
         }
         if (isLoggedIn) {
            std::lock_guard<std::mutex> lk{this->Mutex_};
            while (nextSend <= this->Outbound_.size())
               Send(cli, MakeSoup('S', this->Outbound_[nextSend++ - 1]));
         }
         const auto now = std::chrono::steady_clock::now();
         if (!this->Silent_ && now - lastHb >= std::chrono::milliseconds(100)) {
            Send(cli, MakeSoup('H', std::string{}));
            lastHb = now;
         }
         pollfd pfd{cli, POLLIN, 0};
         if (poll(&pfd, 1, 10) <= 0)
            continue;
         char    buf[4096];
         ssize_t rsz = recv(cli, buf, sizeof(buf), 0);
         if (rsz <= 0)
            return;
         rx.append(buf, static_cast<size_t>(rsz));
         while (rx.size() >= 2) {
            const size_t pksz = 2u + fon9::GetBigEndian<uint16_t>(rx.data());
            if (rx.size() < pksz)
               break;
            const std::string payload = rx.substr(3, pksz - 3);
            switch (rx[2]) {
            case 'L':
            {
               std::lock_guard<std::mutex> lk{this->Mutex_};
               this->Logins_.push_back(rx.substr(0, pksz));
               if (const char reason = this->RejectLogin_) {
                  Send(cli, MakeSoup('J', std::string(1, reason)));
                  break;
               }
               fon9::StrView seq{payload.data() + 26, 20};
               nextSend = fon9::StrTo(fon9::StrTrim(&seq), uint64_t{0});
               if (nextSend == 0)
                  nextSend = this->Outbound_.size() + 1;
               char seqbuf[21];
               snprintf(seqbuf, sizeof(seqbuf), "%20u", static_cast<unsigned>(nextSend));
               Send(cli, MakeSoup('A', std::string{"SES0000001"} + seqbuf));
               isLoggedIn = true;
               break;
            }
            case 'R':
               ++this->HbCount_;
               break;
            case 'U':
            {
               std::lock_guard<std::mutex> lk{this->Mutex_};
               this->Inbound_.push_back(payload);
               this->OnOuchInbound(payload);
               break;
            }
            case 'O':
               return;
            }
            rx.erase(0, pksz);
         }
      }
   }
   // 呼叫前必須已鎖定 Mutex_;
   void OnOuchInbound(const std::string& msg) {
      switch (static_cast<OuchMsgType>(msg[0])) {
      case OuchMsgType::EnterOrder:
         if (msg.size() == sizeof(OuchEnterOrder)) {
            const OuchEnterOrder& req = *reinterpret_cast<const OuchEnterOrder*>(msg.data());
            const std::string     token = ToStrView(req.OrderToken_).ToString();
            if (req.Quantity_.Get() >= 1000000) {
               auto rpt = MakeMsg<OuchOrderRejected>(OuchMsgType::OrderRejected);
               rpt.Timestamp_.Ns_.Put(kTimestampNs);
               rpt.OrderToken_ = req.OrderToken_;
               rpt.RejectCode_.Put(-110023);
               this->Outbound_.push_back(ToStr(rpt));
               return;
            }
            Order& ord = this->Orders_[token];
            ord.OrderBookId_ = req.OrderBookId_.Get();
            ord.Side_ = req.Side_;
            ord.OrderId_ = this->NextOrderId_++;
            ord.Qty_ = req.Quantity_.Get();
            ord.Pri_ = req.Price_.Raw_.Get();
            auto rpt = MakeMsg<OuchOrderAccepted>(OuchMsgType::OrderAccepted);
            rpt.Timestamp_.Ns_.Put(kTimestampNs);
            rpt.OrderToken_ = req.OrderToken_;
            rpt.OrderBookId_ = req.OrderBookId_;
            rpt.Side_ = req.Side_;
            rpt.OrderId_.Put(ord.OrderId_);
            rpt.Quantity_ = req.Quantity_;
            rpt.Price_ = req.Price_;
            rpt.TimeInForce_ = req.TimeInForce_;
            rpt.OpenClose_.Put(0);
            rpt.ClientAccount_ = req.ClientAccount_;
            rpt.OrderState_.Put(static_cast<uint8_t>(OuchOrderState::OnBook));
            rpt.CrossingKey_.Put(0);
            rpt.OrderType_ = req.OrderType_;
            rpt.ShortSellQuantity_ = req.ShortSellQuantity_;
            rpt.MinimumAcceptableQuantity_ = req.MinimumAcceptableQuantity_;
            this->Outbound_.push_back(ToStr(rpt));
         }
         break;
      case OuchMsgType::ReplaceOrder:
         if (msg.size() == sizeof(OuchReplaceOrder)) {
            const OuchReplaceOrder& req = *reinterpret_cast<const OuchReplaceOrder*>(msg.data());
            auto ifind = this->Orders_.find(ToStrView(req.ExistingOrderToken_).ToString());
            if (ifind == this->Orders_.end())
               return;
            Order ord = ifind->second;
            if (req.Quantity_.Get())
               ord.Qty_ = req.Quantity_.Get();
            if (req.Price_.Raw_.Get())
               ord.Pri_ = req.Price_.Raw_.Get();
            this->Orders_[ToStrView(req.ReplacementOrderToken_).ToString()] = ord;
            auto rpt = MakeMsg<OuchOrderReplaced>(OuchMsgType::OrderReplaced);
            rpt.Timestamp_.Ns_.Put(kTimestampNs);
            rpt.ReplacementOrderToken_ = req.ReplacementOrderToken_;
            rpt.PreviousOrderToken_ = req.ExistingOrderToken_;
            rpt.OrderBookId_.Put(ord.OrderBookId_);
            rpt.Side_ = ord.Side_;
            rpt.OrderId_.Put(ord.OrderId_);
            rpt.Quantity_.Put(ord.Qty_);
            rpt.Price_.Raw_.Put(ord.Pri_);
            rpt.TimeInForce_.Put(0);
            rpt.OpenClose_.Put(0);
            rpt.OrderState_.Put(static_cast<uint8_t>(OuchOrderState::OnBook));
            rpt.CrossingKey_.Put(0);
            rpt.OrderType_ = 'Y';
            rpt.ShortSellQuantity_.Put(0);
            rpt.MinimumAcceptableQuantity_.Put(0);
            this->Outbound_.push_back(ToStr(rpt));
         }
         break;
      case OuchMsgType::CancelOrder:
         if (msg.size() == sizeof(OuchCancelOrder)) {
            const OuchCancelOrder& req = *reinterpret_cast<const OuchCancelOrder*>(msg.data());
            auto ifind = this->Orders_.find(ToStrView(req.OrderToken_).ToString());
            if (ifind == this->Orders_.end())
               return;
            auto rpt = MakeMsg<OuchOrderCancelled>(OuchMsgType::OrderCancelled);
            rpt.Timestamp_.Ns_.Put(kTimestampNs);
            rpt.OrderToken_ = req.OrderToken_;
            rpt.OrderBookId_.Put(ifind->second.OrderBookId_);
            rpt.Side_ = ifind->second.Side_;
            rpt.OrderId_.Put(ifind->second.OrderId_);
            rpt.Reason_.Put(static_cast<uint8_t>(OuchCancelReason::ByUser));
            this->Outbound_.push_back(ToStr(rpt));
         }
         break;
      default:
         break;
      }
   }
};

//--------------------------------------------------------------------------//

struct TestRptHandler : public ExgOuchRptHandler {
   fon9_NON_COPY_NON_MOVE(TestRptHandler);
   std::mutex                 Mutex_;
   std::vector<uint64_t>      SeqNos_;
   std::vector<std::string>   Rpts_;
   std::vector<std::string>   NoReadyLines_;
   TestRptHandler() = default;

   void Add(uint64_t seqNo, const void* msg, unsigned msgsz) {
      std::lock_guard<std::mutex> lk{this->Mutex_};
      this->SeqNos_.push_back(seqNo);
      this->Rpts_.push_back(std::string{static_cast<const char*>(msg), msgsz});
   }
   size_t Count() {
      std::lock_guard<std::mutex> lk{this->Mutex_};
      return this->Rpts_.size();
   }
   std::string Rpt(size_t idx) {
      std::lock_guard<std::mutex> lk{this->Mutex_};
      return idx < this->Rpts_.size() ? this->Rpts_[idx] : std::string{};
   }
   uint64_t SeqNo(size_t idx) {
      std::lock_guard<std::mutex> lk{this->Mutex_};
      return idx < this->SeqNos_.size() ? this->SeqNos_[idx] : 0;
   }
   void OnOuchAccepted(ExgOuchTradingLine&, uint64_t seqNo, const OuchOrderAccepted& rpt) override {
      this->Add(seqNo, &rpt, sizeof(rpt));
   }
   void OnOuchRejected(ExgOuchTradingLine&, uint64_t seqNo, const OuchOrderRejected& rpt) override {
      this->Add(seqNo, &rpt, sizeof(rpt));
   }
   void OnOuchReplaced(ExgOuchTradingLine&, uint64_t seqNo, const OuchOrderReplaced& rpt) override {
      this->Add(seqNo, &rpt, sizeof(rpt));
   }
   void OnOuchCancelled(ExgOuchTradingLine&, uint64_t seqNo, const OuchOrderCancelled& rpt) override {
      this->Add(seqNo, &rpt, sizeof(rpt));
   }
   void OnOuchExecuted(ExgOuchTradingLine&, uint64_t seqNo, const OuchOrderExecuted& rpt) override {
      this->Add(seqNo, &rpt, sizeof(rpt));
   }
   void OnOuchUnknown(ExgOuchTradingLine&, uint64_t seqNo, const void* msg, unsigned msgsz) override {
      this->Add(seqNo, msg, msgsz);
   }
   void OnOuchNoReadyLine(f9fmkt::TradingRequest& req, fon9::StrView cause) override {
      (void)req;
      std::lock_guard<std::mutex> lk{this->Mutex_};
      this->NoReadyLines_.push_back(cause.ToString());
   }
};
using TestRptHandlerSP = fon9::intrusive_ptr<TestRptHandler>;

//--------------------------------------------------------------------------//

static void TestFormat() {
   std::cout << "--- Format ---" << std::endl;
   // Login Request: Username(6), Password(10), Session(10, 右靠), SeqNo(20, 右靠).
   const std::string login = MakeSoupLoginRequest("user01", "pw", "SES1", 12);
   fon9_CheckTestResult("Soup.Login", login.size() == 49
                        && login.compare(0, 3, std::string{"\0\x2f" "L", 3}) == 0
                        && login.compare(3, 16, "user01pw        ") == 0
                        && login.compare(19, 10, "      SES1") == 0
                        && login.compare(29, 20, "                  12") == 0);
   fon9_CheckTestResult("Soup.Heartbeat", MakeSoupClientHeartbeat() == std::string("\0\x01" "R", 3));

   OuchTokenGen tokenGen;
   tokenGen.Reset("AB", 123);
   fon9_CheckTestResult("TokenGen", ToStrView(tokenGen.Fetch()) == "AB000000000123"
                        && ToStrView(tokenGen.Fetch()) == "AB000000000124");
   tokenGen.Reset("ABCDEF", 1);
   fon9_CheckTestResult("TokenGen.MaxPrefix", ToStrView(tokenGen.Fetch()) == "ABCD0000000001");

   std::string err;
   ExgOuchRequestSP req = MakeOuchRequest("new 1001 B 100 34.25 Acc=ACC001 Tif=3", err);
   fon9_CheckTestResult("Cmd.New", req && req->RxKind() == f9fmkt_RxKind_RequestNew
                        && req->OrderBookId_ == 1001 && req->Side_ == OuchSide::Buy
                        && req->Qty_ == 100 && req->Pri_.GetOrigValue() == 3425
                        && req->TimeInForce_ == OuchTimeInForce::FillAndKill
                        && ToStrView(req->ClientAccount_) == "ACC001"
                        && req->OrderToken_.empty1st() && req->ShortSellQty_ == 0);
   req = MakeOuchRequest("new 1001 T 200 1.5", err);
   fon9_CheckTestResult("Cmd.ShortSell", req && req->Side_ == OuchSide::ShortSell && req->ShortSellQty_ == 200
                        && req->Pri_.GetOrigValue() == 150);
   req = MakeOuchRequest("chg TK0001 0 35", err);
   fon9_CheckTestResult("Cmd.ChgPri", req && req->RxKind() == f9fmkt_RxKind_RequestChgPri
                        && ToStrView(req->ExistingToken_) == "TK0001" && req->Qty_ == 0
                        && req->Pri_.GetOrigValue() == 3500);
   req = MakeOuchRequest("del TK0001", err);
   fon9_CheckTestResult("Cmd.Del", req && req->RxKind() == f9fmkt_RxKind_RequestDelete
                        && ToStrView(req->OrderToken_) == "TK0001");
   fon9_CheckTestResult("Cmd.BadPriDecimals", !MakeOuchRequest("new 1001 B 100 34.255", err)
                        && StartsWith(err, "Invalid price"));
   fon9_CheckTestResult("Cmd.BadSide", !MakeOuchRequest("new 1001 X 100 34", err)
                        && StartsWith(err, "Invalid side"));
   fon9_CheckTestResult("Cmd.BadQty", !MakeOuchRequest("new 1001 B 0 34", err));
   fon9_CheckTestResult("Cmd.BadTif", !MakeOuchRequest("new 1001 B 100 34 Tif=1", err)
                        && StartsWith(err, "Invalid Tif"));
   fon9_CheckTestResult("Cmd.BadOpt", !MakeOuchRequest("new 1001 B 100 34 Xyz=1", err)
                        && StartsWith(err, "Unknown option"));
   fon9_CheckTestResult("Cmd.Unknown", !MakeOuchRequest("query", err));

   ExgOuchLineArgs args;
   fon9_CheckTestResult("Args.Default", ExgOuchLineArgsParser(args, "User=U1|Pass=P1").empty()
                        && args.SeqNo_ == 1 && args.HbInterval_ == fon9::TimeInterval_Second(1)
                        && args.RxTimeout_ == fon9::TimeInterval_Second(15) && args.IsNeedsLog_
                        && args.FcArgs_.FcCount_ == 0);
   fon9_CheckTestResult("Args.All", ExgOuchLineArgsParser(args, "User=U1|Pass=P1|Session=S1|SeqNo=5|Fc=10/500|Hb=0.5|RxTimeout=3|Log=N").empty()
                        && args.Session_ == "S1" && args.SeqNo_ == 5 && args.FcArgs_.FcCount_ == 10
                        && args.FcArgs_.FcTimeMS_ == 500 && args.HbInterval_ == fon9::TimeInterval_Millisecond(500)
                        && args.RxTimeout_ == fon9::TimeInterval_Second(3) && !args.IsNeedsLog_);
   fon9_CheckTestResult("Args.NoUser", !ExgOuchLineArgsParser(args, "Pass=P1").empty());
   fon9_CheckTestResult("Args.LongUser", !ExgOuchLineArgsParser(args, "User=U123456|Pass=P1").empty());
   fon9_CheckTestResult("Args.BadSeqNo", !ExgOuchLineArgsParser(args, "User=U1|Pass=P1|SeqNo=0").empty());

   auto acc = MakeMsg<OuchOrderAccepted>(OuchMsgType::OrderAccepted);
   acc.Timestamp_.Ns_.Put(kTimestampNs);
   acc.OrderToken_.AssignFrom("TK0001");
   acc.OrderBookId_.Put(1001);
   acc.Side_ = 'B';
   acc.OrderId_.Put(77);
   acc.Quantity_.Put(100);
   acc.Price_.Raw_.Put(3425);
   acc.TimeInForce_.Put(0);
   acc.OrderState_.Put(1);
   acc.ClientAccount_.AssignFrom("ACC001");
   const std::string accText = ToText(ToStr(acc), true);
   std::cout << accText << std::endl;
   fon9_CheckTestResult("Text.Accepted", StartsWith(accText, "A|token=TK0001|obid=1001|side=B|ordid=77|qty=100|pri=34.25|tif=0|st=1|acc=ACC001|time=20261015020000.123456"));
   auto exe = MakeMsg<OuchOrderExecuted>(OuchMsgType::OrderExecuted);
   exe.Timestamp_.Ns_.Put(kTimestampNs);
   exe.OrderToken_.AssignFrom("TK0001");
   exe.OrderBookId_.Put(1001);
   exe.TradedQuantity_.Put(20);
   exe.TradePrice_.Raw_.Put(3425);
   exe.MatchId_.MatchNo_.Put(5555);
   exe.MatchId_.ComboGroupId_.Put(0);
   exe.DealSource_.Put(1);
   exe.MatchAttributes_.Put(1);
   const std::string exeText = ToText(ToStr(exe), true);
   std::cout << exeText << std::endl;
   fon9_CheckTestResult("Text.Executed", StartsWith(exeText, "E|token=TK0001|obid=1001|qty=20|pri=34.25|match=5555.0|src=1|attr=1|"));
   fon9_CheckTestResult("Text.BadSize", ToText(std::string{"A123"}, true) == "?|type=A|sz=4");
}

//--------------------------------------------------------------------------//

int main(int argc, char* argv[]) {
   ForceLinkSomething();
   fon9::AutoPrintTestInfo utinfo{"f9setOuch"};
   TestFormat();

   // 在暫存目錄執行, 結束時移除(log, 設定檔...).
   char tmpdir[] = "/tmp/f9setOuch_UT.XXXXXX";
   if (mkdtemp(tmpdir) == nullptr || chdir(tmpdir) != 0) {
      perror("mkdtemp");
      return 3;
   }
   const uint16_t kPort = static_cast<uint16_t>(30000 + (getpid() % 10000) * 3);
   FakeOuchServer server{kPort};

   fon9::seed::MaTreeSP     root{new fon9::seed::MaTree{"f9setOuch_UT"}};
   fon9::seed::PluginsMgrSP plugins{new fon9::seed::PluginsMgr(root, "Plugins")};
   root->Add(plugins, "Plant.Plugins");
   #define _ "\x01"
   plugins->LoadConfigStr(
      "Id"      _ "Enabled" _ "FileName" _ "EntryName"    _ "Args\n"
      "DevTcpc" _ "Y"       _ ""         _ "TcpClient"    _ "Name=TcpClient|AddTo=FpDevice\n"
      "SetOuch" _ "Y"       _ ""         _ "SetOuchTrade" _ "Name=SetOuch|TokenPrefix=T|Io={SvcCfg='ThreadCount=1'}\n"
   );
   ExgOuchTradeSeedSP seed = root->Get<ExgOuchTradeSeed>("SetOuch");
   fon9_CheckTestResult("Plugin.SetOuchTrade", seed.get() != nullptr);
   if (!seed)
      return 3;
   ExgOuchLineMgr&  lineMgr = seed->GetLineMgr();
   TestRptHandlerSP rpts{new TestRptHandler{}};
   lineMgr.SetRptHandler(rpts);

   std::cout << "--- NoReadyLine ---" << std::endl;
   std::string      err;
   ExgOuchRequestSP req = MakeOuchRequest("new 1001 B 100 34.25", err);
   fon9_CheckTestResult("NoReadyLine", seed->SendRequest(*req) == "token=|result=NoReadyLine"
                        && rpts->NoReadyLines_.size() == 1);

   std::cout << "--- Login ---" << std::endl;
   const std::string ioCfg = fon9::RevPrintTo<std::string>(
      "Id"    _ "Enabled" _ "Sch" _ "Session" _ "SessionArgs"                                         _ "Device"    _ "DeviceArgs\n"
      "Ouch1" _ "Y"       _ ""    _ "SetOuch" _ "User=user01|Pass=pw|Hb=0.2|RxTimeout=1|Fc=3/300|Log=Y" _ "TcpClient" _ "127.0.0.1:", kPort, "|ClosedReopen=0.1\n");
   const std::string loadRes = lineMgr.LoadConfigStr(fon9::ToStrView(ioCfg), fon9::TimeInterval_Millisecond(10));
   fon9_CheckTestResult("LoadConfig", loadRes.empty());
   fon9_CheckTestResult("Login.Ready", WaitFor([&]() { return !lineMgr.IsNoReadyLine(); }));
   fon9_CheckTestResult("Login.Request", server.LoginCount() == 1
                        && server.Logins_[0].compare(3, 16, "user01pw        ") == 0
                        && server.Logins_[0].compare(19, 10, "          ") == 0
                        && server.Logins_[0].compare(29, 20, "                   1") == 0);

   std::cout << "--- New ---" << std::endl;
   std::string res = seed->SendRequest(*MakeOuchRequest("new 1001 B 100 34.25 Acc=ACC001", err));
   std::cout << res << std::endl;
   fon9_CheckTestResult("New.Sent", StartsWith(res, "token=T") && res.size() == 6 + kOuchTokenSize + 12
                        && fon9::StrView{&res}.end()[-4] == 'S');
   const std::string token1 = res.substr(6, kOuchTokenSize);
   fon9_CheckTestResult("New.Inbound", WaitFor([&]() { return server.InboundCount() == 1; }));
   {
      const std::string msg = server.Inbound(0);
      const auto&       ord = *reinterpret_cast<const OuchEnterOrder*>(msg.data());
      std::cout << ToText(msg, false) << std::endl;
      fon9_CheckTestResult("New.Fields", msg.size() == sizeof(OuchEnterOrder) && ord.MsgType_ == 'O'
                           && ToStrView(ord.OrderToken_) == fon9::StrView{&token1}
                           && ord.OrderBookId_.Get() == 1001 && ord.Side_ == 'B'
                           && ord.Quantity_.Get() == 100 && ord.Price_.Raw_.Get() == 3425
                           && ord.TimeInForce_.Get() == 0 && ord.OrderType_ == 'Y'
                           && ToStrView(ord.ClientAccount_) == "ACC001"
                           && ord.ShortSellQuantity_.Get() == 0 && ord.CrossingKey_.Get() == 0
                           && ord.RegulatoryData_.CapacityOfParticipant_.Get1st() == ' ');
   }
   fon9_CheckTestResult("New.Accepted", WaitFor([&]() { return rpts->Count() == 1; })
                        && rpts->SeqNo(0) == 1 && rpts->Rpt(0)[0] == 'A');
   const uint64_t orderId = reinterpret_cast<const OuchOrderAccepted*>(rpts->Rpt(0).data())->OrderId_.Get();
   fon9_CheckTestResult("New.AcceptedFields", ToStrView(reinterpret_cast<const OuchOrderAccepted*>(rpts->Rpt(0).data())->OrderToken_)
                        == fon9::StrView{&token1} && orderId == 1000);

   std::cout << "--- Replace ---" << std::endl;
   res = seed->SendRequest(*MakeOuchRequest(fon9::ToStrView(std::string{"chg "} + token1 + " 50 34.50"), err));
   std::cout << res << std::endl;
   const std::string token2 = res.substr(6, kOuchTokenSize);
   fon9_CheckTestResult("Replace.Sent", StartsWith(res, "token=T") && token2 != token1);
   fon9_CheckTestResult("Replace.Replaced", WaitFor([&]() { return rpts->Count() == 2; })
                        && rpts->SeqNo(1) == 2 && rpts->Rpt(1)[0] == 'U');
   {
      const std::string msg = server.Inbound(1);
      const auto&       chg = *reinterpret_cast<const OuchReplaceOrder*>(msg.data());
      const auto&       rpt = *reinterpret_cast<const OuchOrderReplaced*>(rpts->Rpt(1).data());
      std::cout << ToText(msg, false) << std::endl;
      fon9_CheckTestResult("Replace.Fields", msg.size() == sizeof(OuchReplaceOrder) && chg.MsgType_ == 'U'
                           && ToStrView(chg.ExistingOrderToken_) == fon9::StrView{&token1}
                           && ToStrView(chg.ReplacementOrderToken_) == fon9::StrView{&token2}
                           && chg.Quantity_.Get() == 50 && chg.Price_.Raw_.Get() == 3450
                           && ToStrView(rpt.PreviousOrderToken_) == fon9::StrView{&token1}
                           && rpt.OrderId_.Get() == orderId && rpt.Quantity_.Get() == 50
                           && rpt.Price_.Get().GetOrigValue() == 3450);
   }

   std::cout << "--- Executed ---" << std::endl;
   {
      auto exe = MakeMsg<OuchOrderExecuted>(OuchMsgType::OrderExecuted);
      exe.Timestamp_.Ns_.Put(kTimestampNs);
      exe.OrderToken_.AssignFrom(&token2);
      exe.OrderBookId_.Put(1001);
      exe.TradedQuantity_.Put(20);
      exe.TradePrice_.Raw_.Put(3450);
      exe.MatchId_.MatchNo_.Put(5555);
      exe.MatchId_.ComboGroupId_.Put(0);
      exe.DealSource_.Put(1);
      exe.MatchAttributes_.Put(0);
      server.PushOutbound(ToStr(exe));
   }
   fon9_CheckTestResult("Executed", WaitFor([&]() { return rpts->Count() == 3; })
                        && rpts->SeqNo(2) == 3 && rpts->Rpt(2)[0] == 'E'
                        && reinterpret_cast<const OuchOrderExecuted*>(rpts->Rpt(2).data())->TradedQuantity_.Get() == 20);

   std::cout << "--- Cancel ---" << std::endl;
   // 刪單必須使用新單的 Token.
   res = seed->SendRequest(*MakeOuchRequest(fon9::ToStrView("del " + token1), err));
   fon9_CheckTestResult("Cancel.Sent", res == "token=" + token1 + "|result=Sent");
   fon9_CheckTestResult("Cancel.Cancelled", WaitFor([&]() { return rpts->Count() == 4; })
                        && rpts->SeqNo(3) == 4 && rpts->Rpt(3)[0] == 'C'
                        && reinterpret_cast<const OuchOrderCancelled*>(rpts->Rpt(3).data())->Reason_.Get() == 1
                        && server.Inbound(2) == std::string{"X"} + token1);

   std::cout << "--- Rejected ---" << std::endl;
   res = seed->SendRequest(*MakeOuchRequest("new 1001 S 1000000 1", err));
   fon9_CheckTestResult("Rejected", WaitFor([&]() { return rpts->Count() == 5; })
                        && rpts->SeqNo(4) == 5 && rpts->Rpt(4)[0] == 'J'
                        && reinterpret_cast<const OuchOrderRejected*>(rpts->Rpt(4).data())->RejectCode_.Get() == -110023);

   std::cout << "--- Request errors ---" << std::endl;
   {
      ExgOuchRequestSP bad{new ExgOuchRequest{f9fmkt_RxKind_RequestChgQty}}; // 沒有 ExistingToken.
      fon9_CheckTestResult("Reject.NoExistingToken", seed->SendRequest(*bad) == "token=|result=RejectRequest");
   }

   std::cout << "--- Flow control ---" << std::endl;
   // Fc=3/300ms: 連續送出 5 筆, 前面可能還有額度被占用, 至少有 1 筆排隊, 最後全部送出.
   std::this_thread::sleep_for(std::chrono::milliseconds(350));
   const size_t inboundBefore = server.InboundCount();
   unsigned     queuingCount = 0;
   for (unsigned L = 0; L < 5; ++L) {
      res = seed->SendRequest(*MakeOuchRequest("new 1001 B 100 34", err));
      if (res == "token=|result=Queuing")
         ++queuingCount;
   }
   fon9_CheckTestResult("Fc.Queuing", queuingCount == 2);
   fon9_CheckTestResult("Fc.AllSent", WaitFor([&]() { return server.InboundCount() == inboundBefore + 5; })
                        && WaitFor([&]() { return rpts->Count() == 10; }));
   fon9_CheckTestResult("Fc.Interval", ToStrView(reinterpret_cast<const OuchEnterOrder*>(server.Inbound(inboundBefore + 4).data())->OrderToken_)
                        != ToStrView(reinterpret_cast<const OuchEnterOrder*>(server.Inbound(inboundBefore + 3).data())->OrderToken_));

   std::cout << "--- Heartbeat ---" << std::endl;
   const unsigned hbBefore = server.HbCount_;
   fon9_CheckTestResult("Heartbeat", WaitFor([&]() { return server.HbCount_ >= hbBefore + 2; }, 1000));

   std::cout << "--- Reconnect ---" << std::endl;
   // 斷線後重連: 使用同一個 Session, 接續序號 11, 不會重複收到回報.
   server.DropNow_ = true;
   fon9_CheckTestResult("Reconnect.Login", WaitFor([&]() { return server.LoginCount() == 2 && !lineMgr.IsNoReadyLine(); }));
   fon9_CheckTestResult("Reconnect.Request", server.Logins_[1].compare(19, 10, "SES0000001") == 0
                        && server.Logins_[1].compare(29, 20, "                  11") == 0);
   std::this_thread::sleep_for(std::chrono::milliseconds(100));
   fon9_CheckTestResult("Reconnect.NoDup", rpts->Count() == 10);
   res = seed->SendRequest(*MakeOuchRequest("new 1001 B 100 33", err));
   fon9_CheckTestResult("Reconnect.Rpt", WaitFor([&]() { return rpts->Count() == 11; }) && rpts->SeqNo(10) == 11);

   std::cout << "--- RxTimeout ---" << std::endl;
   // 伺服器不送 Heartbeat: 超過 RxTimeout(1秒) 斷線, 然後重新連線.
   server.Silent_ = true;
   fon9_CheckTestResult("RxTimeout.Closed", WaitFor([&]() { return server.ClosedCount_ == 2; }, 3000));
   server.Silent_ = false;
   fon9_CheckTestResult("RxTimeout.Relogin", WaitFor([&]() { return server.LoginCount() == 3 && !lineMgr.IsNoReadyLine(); }));

   std::cout << "--- LoginRejected ---" << std::endl;
   server.RejectLogin_ = 'A';
   server.DropNow_ = true;
   fon9_CheckTestResult("LoginRejected.Retry", WaitFor([&]() { return server.LoginCount() >= 5; }));
   fon9_CheckTestResult("LoginRejected.NoReadyLine", lineMgr.IsNoReadyLine()
                        && seed->SendRequest(*MakeOuchRequest("new 1001 B 100 33", err)) == "token=|result=NoReadyLine");
   server.RejectLogin_ = 0;
   fon9_CheckTestResult("LoginRejected.Recovered", WaitFor([&]() { return !lineMgr.IsNoReadyLine(); }));

   // ----- 結束.
   fon9::GetDefaultTimerThread()->WaitForEndNow();
   root->OnParentSeedClear();
   fon9::WaitDefaultThreadPoolQuit(fon9::GetDefaultThreadPool());
   seed.reset();
   plugins.reset();
   // 線路已全部結束, log 檔已寫完, 才能檢查 log 內容.
   std::cout << "--- Log ---" << std::endl;
   std::string logContent;
   if (system("find . -name 'SetOuch_user01.log' > logname.txt") != 0)
      std::cout << "find failed" << std::endl;
   if (FILE* fd = fopen("logname.txt", "r")) {
      char fname[512];
      if (fgets(fname, sizeof(fname), fd)) {
         fname[strcspn(fname, "\n")] = '\0';
         std::cout << "log=" << fname << std::endl;
         if (FILE* lfd = fopen(fname, "r")) {
            char   buf[4096];
            size_t rsz;
            while ((rsz = fread(buf, 1, sizeof(buf), lfd)) > 0)
               logContent.append(buf, rsz);
            fclose(lfd);
         }
      }
      fclose(fd);
   }
   fon9_CheckTestResult("Log.Content", logContent.find("|Tx|Login|user=user01|session=|seq=1\n") != std::string::npos
                        && logContent.find("|Rx|LoginAccepted|ApReady|session=SES0000001|seq=1\n") != std::string::npos
                        && logContent.find("|Tx|O|token=" + token1 + "|obid=1001|side=B|qty=100|pri=34.25|") != std::string::npos
                        && logContent.find("|Rx|1|A|token=" + token1) != std::string::npos
                        && logContent.find("|Tx|Login|user=user01|session=SES0000001|seq=11\n") != std::string::npos
                        && logContent.find("Login rejected:A(Not authorised)") != std::string::npos);

   if (fon9::IsKeepTestFiles(argc, argv))
      std::cout << "Keep test files: " << tmpdir << std::endl;
   else if (chdir("/") == 0)
      fon9_CheckTestResult("Cleanup", system((std::string{"rm -rf "} + tmpdir).c_str()) == 0);
}

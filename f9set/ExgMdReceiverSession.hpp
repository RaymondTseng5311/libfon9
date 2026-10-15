// \file f9set/ExgMdReceiverSession.hpp
#ifndef __f9set_ExgMdReceiverSession_hpp__
#define __f9set_ExgMdReceiverSession_hpp__
#include "f9set/ExgMdSystem.hpp"
#include "f9set/ExgMdSoupBinTcp.hpp"
#include "fon9/io/Session.hpp"
#include "fon9/FileAppender.hpp"

namespace f9set {

/// MoldUDP64 接收: multicast(Mc) 或 Re-request(Rr).
/// - 收到的封包直接交給 ExgMdSystem::OnMoldPacket(), 由序號整合 A/B 及回補.
/// - Rr: 連線成功後註冊到 ExgMdSystem, 缺號時由此送出 Request Packet.
class f9set_API ExgMdMoldSession : public fon9::io::Session {
   fon9_NON_COPY_NON_MOVE(ExgMdMoldSession);
   using base = fon9::io::Session;
   fon9::AsyncFileAppenderSP  PkLog_;
   fon9::TimeStamp            LastRecvTime_;
   std::string                RxBuf_;

protected:
   bool OnDevice_BeforeOpen(fon9::io::Device& dev, std::string& cfgstr) override;
   void OnDevice_StateChanged(fon9::io::Device& dev, const fon9::io::StateChangedArgs& e) override;
   fon9::io::RecvBufferSize OnDevice_LinkReady(fon9::io::Device& dev) override;
   fon9::io::RecvBufferSize OnDevice_Recv(fon9::io::Device& dev, fon9::DcQueue& rxbuf) override;
   void OnDevice_CommonTimer(fon9::io::Device& dev, fon9::TimeStamp now) override;
   std::string SessionCommand(fon9::io::Device& dev, fon9::StrView cmdln) override;

public:
   const ExgMdSystemSP     MdSys_;
   const fon9::CharVector  PkLogName_;
   const bool              IsRr_;
   uint64_t                PkCount_{0};
   uint64_t                BadPkCount_{0};

   ExgMdMoldSession(ExgMdSystemSP mdsys, bool isRr, fon9::StrView pkLogName)
      : MdSys_{std::move(mdsys)}
      , PkLogName_{pkLogName}
      , IsRr_{isRr} {
   }
   ~ExgMdMoldSession();

   /// 處理一個 UDP payload(可能包含多個 MoldUDP64 封包).
   void OnDgramReceived(const void* pk, size_t pksz);
};

/// GLIMPSE 快照(SoupBinTCP):
/// - 連線成功: ExgMdSystem::GlimpseBegin()(即時行情先暫存), 送出 Login(Seq=1);
/// - Sequenced Data: ExgMdSystem::GlimpseMessage(); 收到 'G' 之後送出 Logout 並關閉連線;
/// - 每秒送出 Client Heartbeat; 超過 15 秒沒收到任何資料則斷線;
/// - 尚未收到 'G' 就斷線: ExgMdSystem::GlimpseAbort().
class f9set_API ExgMdGlimpseSession : public fon9::io::Session, private SoupBinTcpClient {
   fon9_NON_COPY_NON_MOVE(ExgMdGlimpseSession);
   using base = fon9::io::Session;
   fon9::io::Device* CurDev_{nullptr};
   fon9::TimeStamp   LastRecvTime_;
   bool              IsDone_{false};

protected:
   bool OnDevice_BeforeOpen(fon9::io::Device& dev, std::string& cfgstr) override;
   void OnDevice_StateChanged(fon9::io::Device& dev, const fon9::io::StateChangedArgs& e) override;
   fon9::io::RecvBufferSize OnDevice_LinkReady(fon9::io::Device& dev) override;
   fon9::io::RecvBufferSize OnDevice_Recv(fon9::io::Device& dev, fon9::DcQueue& rxbuf) override;
   void OnDevice_CommonTimer(fon9::io::Device& dev, fon9::TimeStamp now) override;
   std::string SessionCommand(fon9::io::Device& dev, fon9::StrView cmdln) override;

   void OnSoupLoginAccepted() override;
   void OnSoupLoginRejected(char reason) override;
   void OnSoupSequencedData(uint64_t seqNo, const void* msg, unsigned msgsz) override;
   void OnSoupEndOfSession() override;

public:
   const ExgMdSystemSP     MdSys_;
   const fon9::CharVector  User_;
   const fon9::CharVector  Pass_;
   uint64_t                SnapshotMsgCount_{0};

   ExgMdGlimpseSession(ExgMdSystemSP mdsys, fon9::StrView user, fon9::StrView pass)
      : MdSys_{std::move(mdsys)}
      , User_{user}
      , Pass_{pass} {
   }
   ~ExgMdGlimpseSession();
};

} // namespaces
#endif//__f9set_ExgMdReceiverSession_hpp__

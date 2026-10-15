// \file f9set/ExgMdReceiverSession.cpp
#include "f9set/ExgMdReceiverSession.hpp"
#include "fon9/io/Device.hpp"
#include "fon9/Log.hpp"

namespace f9set {
using namespace fon9;
static const TimeInterval  kMdHbInterval = TimeInterval_Second(35);
static const TimeInterval  kGlimpseHbInterval = TimeInterval_Second(1);
static const TimeInterval  kGlimpseTimeout = TimeInterval_Second(15);

/// [Length(2 bytes, big-endian)][Packet]; 與 f9setExgMdItch_UT 的重播格式相同.
static void AppendPkLog(AsyncFileAppender& pklog, const void* pk, size_t pksz) {
   const size_t   kSaveSize = pksz + sizeof(uint16_t);
   RevBufferNode* rbuf{RevBufferNode::Alloc(kSaveSize)};
   fon9::byte*    pbuf = rbuf->GetDataBegin() - kSaveSize;
   rbuf->SetPrefixUsed(pbuf);
   PutBigEndian(pbuf, static_cast<uint16_t>(pksz));
   memcpy(pbuf + sizeof(uint16_t), pk, pksz);
   pklog.Append(rbuf);
}
//--------------------------------------------------------------------------//
ExgMdMoldSession::~ExgMdMoldSession() {
}
bool ExgMdMoldSession::OnDevice_BeforeOpen(io::Device& dev, std::string& cfgstr) {
   (void)dev; (void)cfgstr;
   this->PkLog_.reset();
   if (!this->PkLogName_.empty()) {
      auto              pklog = AsyncFileAppender::Make();
      const std::string fname = this->MdSys_->LogPathPre() + "_" + this->PkLogName_.ToString();
      auto              res = pklog->OpenImmediately(fname, FileMode::CreatePath | FileMode::Append);
      if (res.IsError())
         fon9_LOG_ERROR("f9set.ExgMdMoldSession.PkLog|fname=", fname, '|', res);
      else
         this->PkLog_ = std::move(pklog);
   }
   return true;
}
void ExgMdMoldSession::OnDevice_StateChanged(io::Device& dev, const io::StateChangedArgs& e) {
   if (this->IsRr_ && e.BeforeState_ == io::State::LinkReady)
      this->MdSys_->SetRrDevice(nullptr, &dev);
}
io::RecvBufferSize ExgMdMoldSession::OnDevice_LinkReady(io::Device& dev) {
   if (this->IsRr_)
      this->MdSys_->SetRrDevice(&dev);
   this->LastRecvTime_ = UtcNow();
   dev.CommonTimerRunAfter(kMdHbInterval);
   return io::RecvBufferSize::Default;
}
void ExgMdMoldSession::OnDevice_CommonTimer(io::Device& dev, TimeStamp now) {
   dev.CommonTimerRunAfter(kMdHbInterval);
   if (this->IsRr_ || now - this->LastRecvTime_ < kMdHbInterval)
      return;
   // MoldUDP64 每秒都有 Heartbeat; 太久沒收到, 可能線路有問題.
   fon9_LOG_WARN("f9set.ExgMdMoldSession.Hb|dev=", ToPtr(&dev), "|id={", dev.WaitGetDeviceId(), "}"
                 "|interval=", kMdHbInterval, "|info=No data");
}
io::RecvBufferSize ExgMdMoldSession::OnDevice_Recv(io::Device& dev, DcQueue& rxbuf) {
   (void)dev;
   // Dgram: rxbuf 為一個(或多個) UDP payload, 可能分散在多個 block.
   while (!rxbuf.empty()) {
      const size_t sz = rxbuf.CalcSize();
      this->RxBuf_.resize(sz);
      rxbuf.Read(&*this->RxBuf_.begin(), sz);
      this->OnDgramReceived(this->RxBuf_.data(), sz);
   }
   return io::RecvBufferSize::Default;
}
void ExgMdMoldSession::OnDgramReceived(const void* pk, size_t pksz) {
   this->LastRecvTime_ = UtcNow();
   const char* pbeg = static_cast<const char*>(pk);
   while (pksz > 0) {
      const size_t mpksz = MoldUdp64PacketSize(pbeg, pksz);
      if (mpksz == 0) {
         ++this->BadPkCount_;
         this->MdSys_->OnMoldPacket(pbeg, pksz); // 計入 BadPkCount_.
         break;
      }
      ++this->PkCount_;
      if (this->PkLog_)
         AppendPkLog(*this->PkLog_, pbeg, mpksz);
      this->MdSys_->OnMoldPacket(pbeg, mpksz);
      pbeg += mpksz;
      pksz -= mpksz;
   }
}
std::string ExgMdMoldSession::SessionCommand(io::Device& dev, StrView cmdln) {
   (void)dev;
   cmdln = StrFetchTrim(cmdln, &isspace);
   if (cmdln == "info")
      return RevPrintTo<std::string>("pk=", this->PkCount_, "|badPk=", this->BadPkCount_, "|sys:", this->MdSys_->GetInfo());
   return "unknown ExgMdMoldSession command";
}
//--------------------------------------------------------------------------//
ExgMdGlimpseSession::~ExgMdGlimpseSession() {
}
bool ExgMdGlimpseSession::OnDevice_BeforeOpen(io::Device& dev, std::string& cfgstr) {
   (void)cfgstr;
   this->MdSys_->SetGlimpseDevice(&dev);
   return true;
}
void ExgMdGlimpseSession::OnDevice_StateChanged(io::Device& dev, const io::StateChangedArgs& e) {
   if (e.BeforeState_ == io::State::LinkReady && !this->IsDone_)
      this->MdSys_->GlimpseAbort();
   if (e.After_.State_ >= io::State::Disposing)
      this->MdSys_->SetGlimpseDevice(nullptr, &dev);
}
io::RecvBufferSize ExgMdGlimpseSession::OnDevice_LinkReady(io::Device& dev) {
   this->ResetLogin();
   this->IsDone_ = false;
   this->SnapshotMsgCount_ = 0;
   this->LastRecvTime_ = UtcNow();
   this->MdSys_->GlimpseBegin();
   // GLIMPSE 必須從序號 1 開始, Session 空白表示目前的 Session.
   dev.StrSendASAP(MakeLoginRequest(ToStrView(this->User_), ToStrView(this->Pass_), StrView{}, 1));
   dev.CommonTimerRunAfter(kGlimpseHbInterval);
   return io::RecvBufferSize::Default;
}
io::RecvBufferSize ExgMdGlimpseSession::OnDevice_Recv(io::Device& dev, DcQueue& rxbuf) {
   this->LastRecvTime_ = UtcNow();
   this->CurDev_ = &dev;
   const bool isOK = this->FeedBuffer(rxbuf);
   this->CurDev_ = nullptr;
   if (!isOK) {
      dev.AsyncClose("SoupBinTCP bad packet");
      return io::RecvBufferSize::NoRecvEvent;
   }
   return io::RecvBufferSize::Default;
}
void ExgMdGlimpseSession::OnDevice_CommonTimer(io::Device& dev, TimeStamp now) {
   if (dev.OpImpl_GetState() != io::State::LinkReady || this->IsDone_)
      return;
   if (now - this->LastRecvTime_ > kGlimpseTimeout) {
      dev.AsyncClose("GLIMPSE timeout");
      return;
   }
   dev.StrSendASAP(MakeClientHeartbeat());
   dev.CommonTimerRunAfter(kGlimpseHbInterval);
}
void ExgMdGlimpseSession::OnSoupLoginAccepted() {
   fon9_LOG_INFO("f9set.Glimpse.LoginAccepted|session=", this->GetSession(), "|seq=", this->GetNextSeqNo());
}
void ExgMdGlimpseSession::OnSoupLoginRejected(char reason) {
   fon9_LOG_ERROR("f9set.Glimpse.LoginRejected|reason=", reason);
   if (this->CurDev_)
      this->CurDev_->AsyncClose(RevPrintTo<std::string>("GLIMPSE login rejected:", reason));
}
void ExgMdGlimpseSession::OnSoupSequencedData(uint64_t seqNo, const void* msg, unsigned msgsz) {
   if (this->IsDone_)
      return;
   ++this->SnapshotMsgCount_;
   if (!this->MdSys_->GlimpseMessage(seqNo, msg, msgsz))
      return;
   this->IsDone_ = true;
   fon9_LOG_IMP("f9set.Glimpse.Done|msgs=", this->SnapshotMsgCount_);
   if (this->CurDev_) {
      this->CurDev_->StrSendASAP(MakeLogoutRequest());
      this->CurDev_->AsyncLingerClose("GLIMPSE snapshot done");
   }
}
void ExgMdGlimpseSession::OnSoupEndOfSession() {
   if (!this->IsDone_ && this->CurDev_)
      this->CurDev_->AsyncClose("GLIMPSE end of session");
}
std::string ExgMdGlimpseSession::SessionCommand(io::Device& dev, StrView cmdln) {
   (void)dev;
   cmdln = StrFetchTrim(cmdln, &isspace);
   if (cmdln == "info")
      return RevPrintTo<std::string>("msgs=", this->SnapshotMsgCount_, "|done=", this->IsDone_ ? 'Y' : 'N',
                                     "|sys:", this->MdSys_->GetInfo());
   return "unknown ExgMdGlimpseSession command";
}

} // namespaces

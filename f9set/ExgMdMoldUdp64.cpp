// \file f9set/ExgMdMoldUdp64.cpp
#include "f9set/ExgMdMoldUdp64.hpp"

namespace f9set {

size_t MoldUdp64PacketSize(const void* pk, size_t pksz) {
   if (pksz < sizeof(MoldUdp64Head))
      return 0;
   const uint16_t msgCount = reinterpret_cast<const MoldUdp64Head*>(pk)->MsgCount_.Get();
   if (msgCount == kMoldUdp64EndOfSession)
      return sizeof(MoldUdp64Head);
   const fon9::byte* const pbeg = reinterpret_cast<const fon9::byte*>(pk);
   size_t                  pos = sizeof(MoldUdp64Head);
   for (uint16_t L = 0; L < msgCount; ++L) {
      if (pos + sizeof(uint16_t) > pksz)
         return 0;
      pos += sizeof(uint16_t) + fon9::GetBigEndian<uint16_t>(pbeg + pos);
   }
   return pos <= pksz ? pos : 0;
}

MoldUdp64Receiver::~MoldUdp64Receiver() {
}
void MoldUdp64Receiver::ClearStatus() {
   this->PkCount_ = this->BadPkCount_ = 0;
   this->MsgCount_ = this->DupMsgCount_ = 0;
   this->GapCount_ = this->GapMsgCount_ = 0;
   this->RequestCount_ = 0;
}
void MoldUdp64Receiver::ResetSession() {
   this->HasSession_ = false;
   this->IsEndOfSession_ = false;
   this->IsWaitingSnapshot_ = false;
   this->NextSeqNo_ = 0;
   this->HighSeqNo_ = 0;
   this->Pending_.clear();
   this->ResetRecoverState();
}
void MoldUdp64Receiver::OnMoldGap(uint64_t expectedSeqNo, uint64_t receivedSeqNo) {
   (void)expectedSeqNo; (void)receivedSeqNo;
}
void MoldUdp64Receiver::OnMoldLost(uint64_t fromSeqNo, uint64_t toSeqNo) {
   (void)fromSeqNo; (void)toSeqNo;
}
bool MoldUdp64Receiver::OnMoldSendRequest(const MoldUdp64Request& req) {
   (void)req;
   return false;
}
void MoldUdp64Receiver::OnMoldSessionChanged(const MoldUdp64Session& newSession) {
   (void)newSession;
}
void MoldUdp64Receiver::OnMoldEndOfSession() {
}
//--------------------------------------------------------------------------//
void MoldUdp64Receiver::CheckHighSeqNo(uint64_t nextSeqNo) {
   if (this->HighSeqNo_ < nextSeqNo)
      this->HighSeqNo_ = nextSeqNo;
   if (this->IsWaitingSnapshot_ || !this->HasGap())
      return;
   if (!this->IsRecoverEnabled_) {
      // 沒有回補: 通知後直接跳過缺號.
      this->OnMoldGap(this->NextSeqNo_, this->HighSeqNo_);
      this->SkipGap();
      return;
   }
   if (this->GapBeginTime_.IsNull()) {
      this->GapBeginTime_ = fon9::UtcNow();
      this->OnMoldGap(this->NextSeqNo_, this->GetGapEndSeqNo());
   }
}
void MoldUdp64Receiver::DrainPending() {
   while (!this->Pending_.empty()) {
      auto ibeg = this->Pending_.begin();
      if (ibeg->first > this->NextSeqNo_)
         break;
      const uint64_t seqNo = ibeg->first;
      std::string    msg = std::move(ibeg->second);
      this->Pending_.erase(ibeg);
      if (seqNo < this->NextSeqNo_)
         ++this->DupMsgCount_;
      else
         this->ProcessMessage(seqNo, msg.data(), static_cast<unsigned>(msg.size()));
   }
   if (!this->HasGap())
      this->ResetRecoverState();
}
void MoldUdp64Receiver::SkipGap() {
   if (!this->HasGap())
      return;
   const uint64_t gapEnd = this->GetGapEndSeqNo();
   const uint64_t lostFrom = this->NextSeqNo_;
   ++this->GapCount_;
   this->GapMsgCount_ += gapEnd - lostFrom;
   this->NextSeqNo_ = gapEnd;
   this->OnMoldLost(lostFrom, gapEnd);
   this->ResetRecoverState();
   this->DrainPending();
   if (this->HasGap() && this->IsRecoverEnabled_) {
      // 還有下一個缺號.
      this->GapBeginTime_ = fon9::UtcNow();
      this->OnMoldGap(this->NextSeqNo_, this->GetGapEndSeqNo());
   }
}
fon9::TimeInterval MoldUdp64Receiver::CheckRecover(fon9::TimeStamp now) {
   if (this->IsWaitingSnapshot_ || !this->HasGap())
      return fon9::TimeInterval{};
   if (!this->IsRecoverEnabled_) {
      this->SkipGap();
      return fon9::TimeInterval{};
   }
   if (this->GapBeginTime_.IsNull())
      this->GapBeginTime_ = now;
   const fon9::TimeInterval gapElapsed = now - this->GapBeginTime_;
   if (gapElapsed < this->RecoverArgs_.GapWait_)
      return this->RecoverArgs_.GapWait_ - gapElapsed;
   if (this->LastRequestSeqNo_ == this->NextSeqNo_) {
      // 上次要求之後, 沒有任何進展.
      const fon9::TimeInterval reqElapsed = now - this->LastRequestTime_;
      if (reqElapsed < this->RecoverArgs_.RetryInterval_)
         return this->RecoverArgs_.RetryInterval_ - reqElapsed;
      if (this->RetryCount_ >= this->RecoverArgs_.MaxRetry_) {
         this->SkipGap();
         return this->HasGap() ? this->RecoverArgs_.GapWait_ : fon9::TimeInterval{};
      }
   }
   else {
      this->RetryCount_ = 0;
   }
   const uint64_t gapCount = this->GetGapEndSeqNo() - this->NextSeqNo_;
   MoldUdp64Request req;
   req.Session_ = this->Session_;
   req.SeqNo_.Put(this->NextSeqNo_);
   req.MsgCount_.Put(static_cast<uint16_t>(gapCount < this->RecoverArgs_.MaxRequestCount_
                                           ? gapCount : this->RecoverArgs_.MaxRequestCount_));
   if (!this->OnMoldSendRequest(req)) {
      this->SkipGap();
      return this->HasGap() ? this->RecoverArgs_.GapWait_ : fon9::TimeInterval{};
   }
   ++this->RequestCount_;
   ++this->RetryCount_;
   this->LastRequestSeqNo_ = this->NextSeqNo_;
   this->LastRequestTime_ = now;
   return this->RecoverArgs_.RetryInterval_;
}
//--------------------------------------------------------------------------//
void MoldUdp64Receiver::BeginSnapshot() {
   this->IsWaitingSnapshot_ = true;
   this->ResetRecoverState();
}
void MoldUdp64Receiver::EndSnapshot(uint64_t nextSeqNo) {
   this->IsWaitingSnapshot_ = false;
   this->NextSeqNo_ = nextSeqNo;
   if (this->HighSeqNo_ < nextSeqNo)
      this->HighSeqNo_ = nextSeqNo;
   this->DrainPending();
   this->CheckHighSeqNo(this->HighSeqNo_);
}
void MoldUdp64Receiver::AbortSnapshot() {
   this->IsWaitingSnapshot_ = false;
   if (!this->Pending_.empty() && this->NextSeqNo_ < this->Pending_.begin()->first)
      this->NextSeqNo_ = this->Pending_.begin()->first;
   this->DrainPending();
   this->CheckHighSeqNo(this->HighSeqNo_);
}
//--------------------------------------------------------------------------//
MoldUdp64Result MoldUdp64Receiver::FeedPacket(const void* pk, size_t pksz) {
   ++this->PkCount_;
   if (fon9_UNLIKELY(MoldUdp64PacketSize(pk, pksz) == 0)) {
      ++this->BadPkCount_;
      return MoldUdp64Result::BadPacket;
   }
   const MoldUdp64Head& head = *reinterpret_cast<const MoldUdp64Head*>(pk);
   const uint64_t       pkSeqNo = head.SeqNo_.Get();
   const uint16_t       msgCount = head.MsgCount_.Get();
   if (fon9_UNLIKELY(!this->HasSession_ || this->Session_ != head.Session_)) {
      if (this->HasSession_) {
         // 換了新的 Session: 序號從 1 開始, 若第一個收到的封包不是 1, 則視為缺號.
         this->OnMoldSessionChanged(head.Session_);
         this->NextSeqNo_ = this->HighSeqNo_ = 1;
         this->Pending_.clear();
         this->ResetRecoverState();
      }
      else if (this->NextSeqNo_ == 0) {
         // 第一次收到封包, 且沒有用 SetNextSeqNo() 指定: 從此封包開始接收.
         // (盤中啟動時, 之前的委託簿須由 GLIMPSE 快照取得)
         this->NextSeqNo_ = pkSeqNo;
      }
      this->HasSession_ = true;
      this->Session_ = head.Session_;
      this->IsEndOfSession_ = false;
   }
   if (msgCount == kMoldUdp64Heartbeat || msgCount == kMoldUdp64EndOfSession) {
      // Heartbeat 與 End of Session 的 SeqNo = 下一個訊息序號, 可用來偵測缺號.
      this->CheckHighSeqNo(pkSeqNo);
      if (msgCount == kMoldUdp64Heartbeat)
         return MoldUdp64Result::Heartbeat;
      if (!this->IsEndOfSession_) {
         this->IsEndOfSession_ = true;
         this->OnMoldEndOfSession();
      }
      return MoldUdp64Result::EndOfSession;
   }
   MoldUdp64Result   res = MoldUdp64Result::Duplicate;
   uint64_t          seqNo = pkSeqNo;
   const fon9::byte* pmsg = reinterpret_cast<const fon9::byte*>(pk) + sizeof(MoldUdp64Head);
   for (uint16_t L = 0; L < msgCount; ++L, ++seqNo) {
      const unsigned msgsz = fon9::GetBigEndian<uint16_t>(pmsg);
      const void*    msg = pmsg + sizeof(uint16_t);
      pmsg += sizeof(uint16_t) + msgsz;
      if (fon9_UNLIKELY(this->IsWaitingSnapshot_
                        || (seqNo > this->NextSeqNo_ && this->IsRecoverEnabled_))) {
         if (seqNo < this->NextSeqNo_ && !this->IsWaitingSnapshot_) {
            ++this->DupMsgCount_;
            continue;
         }
         if (!this->Pending_.emplace(seqNo, std::string{static_cast<const char*>(msg), msgsz}).second) {
            ++this->DupMsgCount_;
            continue;
         }
         if (res != MoldUdp64Result::Ok)
            res = MoldUdp64Result::Pending;
         this->CheckHighSeqNo(seqNo + 1);
         if (this->Pending_.size() > this->RecoverArgs_.MaxPendingCount_) {
            if (this->IsWaitingSnapshot_)
               this->AbortSnapshot();
            else
               this->SkipGap();
         }
         continue;
      }
      if (seqNo < this->NextSeqNo_) {
         ++this->DupMsgCount_;
         continue;
      }
      if (seqNo > this->NextSeqNo_) // 沒有啟用回補: 通知缺號後跳過.
         this->CheckHighSeqNo(seqNo);
      this->ProcessMessage(seqNo, msg, msgsz);
      res = MoldUdp64Result::Ok;
      if (!this->Pending_.empty())
         this->DrainPending();
   }
   return res;
}

} // namespaces

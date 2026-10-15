// \file f9set/ExgMdMoldUdp64.cpp
#include "f9set/ExgMdMoldUdp64.hpp"

namespace f9set {

MoldUdp64Receiver::~MoldUdp64Receiver() {
}
void MoldUdp64Receiver::ClearStatus() {
   this->PkCount_ = this->BadPkCount_ = 0;
   this->MsgCount_ = this->DupMsgCount_ = 0;
   this->GapCount_ = this->GapMsgCount_ = 0;
}
void MoldUdp64Receiver::OnMoldGap(uint64_t expectedSeqNo, uint64_t receivedSeqNo) {
   (void)expectedSeqNo; (void)receivedSeqNo;
}
void MoldUdp64Receiver::OnMoldSessionChanged(const MoldUdp64Session& newSession) {
   (void)newSession;
}
void MoldUdp64Receiver::OnMoldEndOfSession() {
}

MoldUdp64Result MoldUdp64Receiver::FeedPacket(const void* pk, size_t pksz) {
   ++this->PkCount_;
   if (fon9_UNLIKELY(pksz < sizeof(MoldUdp64Head))) {
      ++this->BadPkCount_;
      return MoldUdp64Result::BadPacket;
   }
   const MoldUdp64Head& head = *reinterpret_cast<const MoldUdp64Head*>(pk);
   const uint64_t       pkSeqNo = head.SeqNo_.Get();
   const uint16_t       msgCount = head.MsgCount_.Get();
   // 先檢查整個封包的 Message Block 是否完整, 避免處理到一半才發現封包有誤.
   const fon9::byte*    pbeg = reinterpret_cast<const fon9::byte*>(pk) + sizeof(MoldUdp64Head);
   const fon9::byte*    pend = reinterpret_cast<const fon9::byte*>(pk) + pksz;
   if (msgCount != kMoldUdp64EndOfSession) {
      const fon9::byte* pmsg = pbeg;
      for (uint16_t L = 0; L < msgCount; ++L) {
         if (pmsg + sizeof(uint16_t) > pend
             || (pmsg += sizeof(uint16_t) + fon9::GetBigEndian<uint16_t>(pmsg)) > pend) {
            ++this->BadPkCount_;
            return MoldUdp64Result::BadPacket;
         }
      }
   }
   if (fon9_UNLIKELY(!this->HasSession_ || this->Session_ != head.Session_)) {
      if (this->HasSession_) {
         // 換了新的 Session: 序號從 1 開始, 若第一個收到的封包不是 1, 則視為缺號.
         this->OnMoldSessionChanged(head.Session_);
         this->NextSeqNo_ = 1;
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
      if (pkSeqNo > this->NextSeqNo_) {
         ++this->GapCount_;
         this->GapMsgCount_ += pkSeqNo - this->NextSeqNo_;
         this->OnMoldGap(this->NextSeqNo_, pkSeqNo);
         this->NextSeqNo_ = pkSeqNo;
      }
      if (msgCount == kMoldUdp64Heartbeat)
         return MoldUdp64Result::Heartbeat;
      if (!this->IsEndOfSession_) {
         this->IsEndOfSession_ = true;
         this->OnMoldEndOfSession();
      }
      return MoldUdp64Result::EndOfSession;
   }
   MoldUdp64Result res = MoldUdp64Result::Duplicate;
   uint64_t        seqNo = pkSeqNo;
   for (uint16_t L = 0; L < msgCount; ++L, ++seqNo) {
      const unsigned msgsz = fon9::GetBigEndian<uint16_t>(pbeg);
      pbeg += sizeof(uint16_t);
      const fon9::byte* pmsg = pbeg;
      pbeg += msgsz;
      if (seqNo < this->NextSeqNo_) {
         ++this->DupMsgCount_;
         continue;
      }
      if (seqNo > this->NextSeqNo_) {
         ++this->GapCount_;
         this->GapMsgCount_ += seqNo - this->NextSeqNo_;
         this->OnMoldGap(this->NextSeqNo_, seqNo);
      }
      this->NextSeqNo_ = seqNo + 1;
      ++this->MsgCount_;
      res = MoldUdp64Result::Ok;
      this->OnMoldMessage(seqNo, pmsg, msgsz);
   }
   return res;
}

} // namespaces

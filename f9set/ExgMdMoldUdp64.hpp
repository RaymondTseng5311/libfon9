// \file f9set/ExgMdMoldUdp64.hpp
//
// MoldUDP64 Downstream Packet 解析.
// 參考: ASX Trade ITCH Specification, Appendix 2 – MoldUDP64 Protocol Specification.
//
#ifndef __f9set_ExgMdMoldUdp64_hpp__
#define __f9set_ExgMdMoldUdp64_hpp__
#include "f9set/ExgMdItchFmt.hpp"
#include "fon9/TimeStamp.hpp"

fon9_BEFORE_INCLUDE_STD
#include <map>
#include <string>
fon9_AFTER_INCLUDE_STD

namespace f9set {
fon9_PACK(1);

using MoldUdp64Session = fon9::CharAryF<10, char, ' '>;

/// MoldUDP64 Downstream Packet Header, 之後接著 MsgCount_ 個 Message Block:
/// - Message Length: 2 bytes(big-endian), 不含此 2 bytes;
/// - Message Data: Message Length bytes;
struct MoldUdp64Head {
   MoldUdp64Session  Session_;
   /// 封包內第一個訊息的序號, 之後的訊息序號依序遞增.
   ItchNum<uint64_t> SeqNo_;
   /// 0 = Heartbeat; 0xffff = End of Session; 其餘為訊息數量.
   ItchNum<uint16_t> MsgCount_;
};
static_assert(sizeof(MoldUdp64Head) == 20, "MoldUdp64Head 沒有 pack?");

/// Request Packet: 送到 Re-request server 要求回補 [SeqNo_, SeqNo_ + MsgCount_).
using MoldUdp64Request = MoldUdp64Head;

fon9_PACK_POP;

enum : uint16_t {
   kMoldUdp64Heartbeat = 0,
   kMoldUdp64EndOfSession = 0xffff,
   /// MoldUDP64 Downstream Packet 最大長度(Genium INET: 1472).
   kMoldUdp64MaxPacketSize = 1472,
};

/// 計算 [pk, pk + pksz) 開頭的 MoldUDP64 封包長度.
/// \retval 0  資料不足一個完整封包.
f9set_API size_t MoldUdp64PacketSize(const void* pk, size_t pksz);

enum class MoldUdp64Result {
   /// 封包內至少有一個新訊息已處理.
   Ok,
   Heartbeat,
   EndOfSession,
   /// 封包內的訊息全都收過了(例如: A/B 雙路備援, 或回補時收到即時封包).
   Duplicate,
   /// 封包內的新訊息都暫存起來了: 等待缺號回補, 或等待 GLIMPSE 快照.
   Pending,
   /// 封包格式有誤(長度不足, 或 Message Block 超過封包長度).
   BadPacket,
};

/// MoldUDP64 缺號回補的參數.
struct MoldUdp64RecoverArgs {
   /// 發現缺號後, 先等一下(另一路 multicast 可能補上), 才送出回補要求.
   fon9::TimeInterval   GapWait_{fon9::TimeInterval_Millisecond(20)};
   /// 送出回補要求後, 等多久沒補上就重送.
   fon9::TimeInterval   RetryInterval_{fon9::TimeInterval_Millisecond(500)};
   /// 同一個缺號最多送幾次回補要求, 超過就放棄(視為遺失).
   unsigned             MaxRetry_{3};
   /// 一次最多要求幾筆; 伺服器只會回一個封包能放得下的數量, 不足的部分會再次要求.
   uint16_t             MaxRequestCount_{1000};
   /// 暫存訊息超過此數量, 就放棄最前面的缺號.
   size_t               MaxPendingCount_{1000 * 1000};
};

/// 解析 MoldUDP64 Downstream Packet, 處理:
/// - 訊息序號: 跳過已收過的訊息(A/B 雙路可同時餵入);
/// - Heartbeat / End of Session;
/// - Session 改變(例如: 換日) 時, 通知 OnMoldSessionChanged() 並從新 Session 的序號 1 開始;
/// - 缺號:
///   - 沒有啟用回補(預設): 通知 OnMoldGap() 後直接跳過;
///   - 啟用回補(SetRecoverEnabled(true)): 缺號之後的訊息先暫存, 由 CheckRecover() 送出回補要求,
///     補齊後依序處理暫存的訊息; 回補多次失敗則放棄缺號.
///     回補的回覆是一般的 Downstream Packet, 同樣透過 FeedPacket() 餵入.
/// - 等待快照(BeginSnapshot() ... EndSnapshot()): 期間收到的訊息全部暫存,
///   EndSnapshot(nextSeqNo) 時丟棄快照已包含的訊息, 再依序處理.
/// - 本身不加鎖, 由使用者確保 thread safe.
class f9set_API MoldUdp64Receiver {
   fon9_NON_COPY_NON_MOVE(MoldUdp64Receiver);
public:
   MoldUdp64Receiver() = default;
   virtual ~MoldUdp64Receiver();

   /// 餵入一個完整的 UDP payload(一個 MoldUDP64 Downstream Packet).
   MoldUdp64Result FeedPacket(const void* pk, size_t pksz);

   /// 下一個期望的訊息序號. 0 表示尚未收到任何封包.
   uint64_t GetNextSeqNo() const {
      return this->NextSeqNo_;
   }
   /// 重新啟動時, 可設定要從哪個序號開始接收; 之前的序號都視為已收過.
   void SetNextSeqNo(uint64_t seqNo) {
      this->NextSeqNo_ = seqNo;
   }
   const MoldUdp64Session& GetSession() const {
      return this->Session_;
   }
   bool HasSession() const {
      return this->HasSession_;
   }
   bool IsEndOfSession() const {
      return this->IsEndOfSession_;
   }

   // ----- 缺號回補.
   MoldUdp64RecoverArgs RecoverArgs_;
   void SetRecoverEnabled(bool v) {
      this->IsRecoverEnabled_ = v;
   }
   bool IsRecoverEnabled() const {
      return this->IsRecoverEnabled_;
   }
   /// 是否有缺號: [GetNextSeqNo(), GetGapEndSeqNo()) 尚未收到.
   bool HasGap() const {
      return this->NextSeqNo_ < this->HighSeqNo_;
   }
   uint64_t GetGapEndSeqNo() const {
      return this->Pending_.empty() ? this->HighSeqNo_ : this->Pending_.begin()->first;
   }
   size_t GetPendingCount() const {
      return this->Pending_.size();
   }
   /// 放棄目前的缺號(視為遺失), 繼續處理之後暫存的訊息.
   void SkipGap();
   /// 檢查是否需要送出回補要求, 或放棄缺號; 透過 OnMoldSendRequest() 送出回補要求.
   /// \retval 下次需要檢查的時間間隔; TimeInterval{} 表示目前沒有缺號, 不用再檢查.
   fon9::TimeInterval CheckRecover(fon9::TimeStamp now);

   // ----- 快照.
   /// 開始等待快照: 之後收到的訊息全部暫存.
   void BeginSnapshot();
   /// 快照完成, nextSeqNo = 快照之後的第一個即時訊息序號.
   void EndSnapshot(uint64_t nextSeqNo);
   /// 快照失敗(例如: 斷線), 放棄等待: 從暫存的第一個訊息開始處理.
   void AbortSnapshot();
   bool IsWaitingSnapshot() const {
      return this->IsWaitingSnapshot_;
   }

   void ClearStatus();
   /// 換日: 清除 Session, 序號, 暫存訊息.
   void ResetSession();

   uint64_t PkCount_{0};
   uint64_t BadPkCount_{0};
   uint64_t MsgCount_{0};
   uint64_t DupMsgCount_{0};
   /// 確定遺失(沒有回補, 或回補失敗)的缺號.
   uint64_t GapCount_{0};
   uint64_t GapMsgCount_{0};
   /// 送出的回補要求次數.
   uint64_t RequestCount_{0};

protected:
   /// 依序收到的訊息; msg 不含 Message Length 欄位.
   virtual void OnMoldMessage(uint64_t seqNo, const void* msg, unsigned msgsz) = 0;
   /// 發現缺號: [expectedSeqNo, receivedSeqNo) 沒收到.
   /// - 沒有啟用回補: 直接從 receivedSeqNo 繼續.
   /// - 有啟用回補: 只是通知, 回補要求由 CheckRecover() 送出; 衍生者可在此啟動計時器.
   virtual void OnMoldGap(uint64_t expectedSeqNo, uint64_t receivedSeqNo);
   /// 缺號確定遺失: [fromSeqNo, toSeqNo).
   virtual void OnMoldLost(uint64_t fromSeqNo, uint64_t toSeqNo);
   /// 送出回補要求. 傳回 false 表示無法送出(例如: 沒有可用的回補線路).
   virtual bool OnMoldSendRequest(const MoldUdp64Request& req);
   /// 收到新的 Session 時通知, 此時 GetSession() 仍為舊的 Session.
   virtual void OnMoldSessionChanged(const MoldUdp64Session& newSession);
   virtual void OnMoldEndOfSession();

private:
   MoldUdp64Session  Session_;
   uint64_t          NextSeqNo_{0};
   /// 已知存在的最大序號+1; 用來判斷是否有缺號.
   uint64_t          HighSeqNo_{0};
   bool              IsEndOfSession_{false};
   bool              HasSession_{false};
   bool              IsRecoverEnabled_{false};
   bool              IsWaitingSnapshot_{false};

   // 缺號回補狀態.
   fon9::TimeStamp   GapBeginTime_{fon9::TimeStamp::Null()};
   fon9::TimeStamp   LastRequestTime_;
   uint64_t          LastRequestSeqNo_{0};
   unsigned          RetryCount_{0};

   using Pendings = std::map<uint64_t, std::string>;
   Pendings          Pending_;

   void CheckHighSeqNo(uint64_t nextSeqNo);
   void ProcessMessage(uint64_t seqNo, const void* msg, unsigned msgsz) {
      this->NextSeqNo_ = seqNo + 1;
      if (this->HighSeqNo_ < this->NextSeqNo_)
         this->HighSeqNo_ = this->NextSeqNo_;
      ++this->MsgCount_;
      this->OnMoldMessage(seqNo, msg, msgsz);
   }
   void DrainPending();
   void ResetRecoverState() {
      this->GapBeginTime_.AssignNull();
      this->LastRequestSeqNo_ = 0;
      this->RetryCount_ = 0;
   }
};

} // namespaces
#endif//__f9set_ExgMdMoldUdp64_hpp__

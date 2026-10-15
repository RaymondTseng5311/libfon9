// \file f9set/ExgMdMoldUdp64.hpp
//
// MoldUDP64 Downstream Packet 解析.
// 參考: ASX Trade ITCH Specification, Appendix 2 – MoldUDP64 Protocol Specification.
//
#ifndef __f9set_ExgMdMoldUdp64_hpp__
#define __f9set_ExgMdMoldUdp64_hpp__
#include "f9set/ExgMdItchFmt.hpp"

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

enum class MoldUdp64Result {
   /// 封包內至少有一個新訊息.
   Ok,
   Heartbeat,
   EndOfSession,
   /// 封包內的訊息全都收過了(例如: A/B 雙路備援, 或回補時收到即時封包).
   Duplicate,
   /// 封包格式有誤(長度不足, 或 Message Block 超過封包長度).
   BadPacket,
};

/// 解析 MoldUDP64 Downstream Packet, 處理:
/// - 訊息序號: 跳過已收過的訊息(A/B 雙路可同時餵入), 發現缺號時通知 OnMoldGap();
/// - Heartbeat / End of Session;
/// - Session 改變(例如: 換日) 時, 通知 OnMoldSessionChanged() 並從新 Session 的序號開始;
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
   bool IsEndOfSession() const {
      return this->IsEndOfSession_;
   }

   void ClearStatus();

   uint64_t PkCount_{0};
   uint64_t BadPkCount_{0};
   uint64_t MsgCount_{0};
   uint64_t DupMsgCount_{0};
   uint64_t GapCount_{0};
   uint64_t GapMsgCount_{0};

protected:
   /// 依序收到的訊息; msg 不含 Message Length 欄位.
   virtual void OnMoldMessage(uint64_t seqNo, const void* msg, unsigned msgsz) = 0;
   /// 發現缺號: [expectedSeqNo, receivedSeqNo) 沒收到.
   /// 預設: 不回補, 直接從 receivedSeqNo 繼續. 衍生者可在此送出 MoldUdp64Request.
   virtual void OnMoldGap(uint64_t expectedSeqNo, uint64_t receivedSeqNo);
   /// 收到新的 Session 時通知, 此時 GetSession() 仍為舊的 Session.
   virtual void OnMoldSessionChanged(const MoldUdp64Session& newSession);
   virtual void OnMoldEndOfSession();

private:
   MoldUdp64Session  Session_;
   uint64_t          NextSeqNo_{0};
   bool              IsEndOfSession_{false};
   bool              HasSession_{false};
};

} // namespaces
#endif//__f9set_ExgMdMoldUdp64_hpp__

// \file f9set/ExgMdItchParser.hpp
#ifndef __f9set_ExgMdItchParser_hpp__
#define __f9set_ExgMdItchParser_hpp__
#include "f9set/ExgMdMoldUdp64.hpp"
#include "fon9/TimeStamp.hpp"

namespace f9set {

/// 將 MoldUDP64 收到的 ITCH 訊息, 依 MsgType 分派到對應的 OnItch*() 處理.
/// - 訊息長度不足時不分派, 計入 ShortMsgCount_;
/// - 訊息長度超過定義時仍分派(保留未來擴充欄位的相容性);
/// - 不認識的 MsgType 分派到 OnItchUnknown();
/// - 'T'(Seconds) 由此處理, 可透過 GetItchTime() 取得訊息時間.
class f9set_API ExgMdItchParser : public MoldUdp64Receiver {
   fon9_NON_COPY_NON_MOVE(ExgMdItchParser);
   using base = MoldUdp64Receiver;
public:
   ExgMdItchParser();
   virtual ~ExgMdItchParser();

   void ClearStatus();

   /// 最近一次 'T' 的 Unix time(UTC) 秒數.
   uint32_t GetItchSeconds() const {
      return this->ItchSeconds_;
   }
   /// 訊息時間(UTC) = 最近一次的 'T' + 訊息的 Nanoseconds_. fon9::TimeStamp 精確到 us.
   fon9::TimeStamp GetItchTime(const ItchMsgHead& msg) const {
      return fon9::TimeStamp{fon9::TimeInterval_Second(this->ItchSeconds_)
                           + fon9::TimeInterval_Microsecond(msg.Nanoseconds_.Get() / 1000)};
   }

   /// 依 MsgType 統計收到的訊息數量.
   uint64_t MsgTypeCount_[256];
   uint64_t ShortMsgCount_{0};
   uint64_t UnknownMsgCount_{0};

   /// 可直接餵入一個 ITCH 訊息(例如: 來自 GLIMPSE 快照, 或測試).
   void ParseItchMessage(uint64_t seqNo, const void* msg, unsigned msgsz);

protected:
   void OnMoldMessage(uint64_t seqNo, const void* msg, unsigned msgsz) override;

   // 預設什麼都不做, 由衍生者覆寫需要的訊息.
   virtual void OnItchSystemEvent(const ItchSystemEvent& msg);
   virtual void OnItchOrderBookDirectory(const ItchOrderBookDirectory& msg);
   virtual void OnItchTickSize(const ItchTickSize& msg);
   virtual void OnItchOrderBookState(const ItchOrderBookState& msg);
   virtual void OnItchAddOrder(const ItchAddOrder& msg, const ItchAlpha<7>* participantId);
   virtual void OnItchOrderExecuted(const ItchOrderExecuted& msg);
   virtual void OnItchOrderExecutedWithPrice(const ItchOrderExecutedWithPrice& msg);
   virtual void OnItchOrderReplace(const ItchOrderReplace& msg);
   virtual void OnItchOrderDelete(const ItchOrderDelete& msg);
   virtual void OnItchTrade(const ItchTrade& msg);
   virtual void OnItchEquilibriumPriceUpdate(const ItchEquilibriumPriceUpdate& msg);
   virtual void OnItchEndOfSnapshot(const ItchEndOfSnapshot& msg);
   virtual void OnItchUnknown(uint64_t seqNo, const void* msg, unsigned msgsz);

   /// 目前處理中的訊息序號.
   uint64_t CurSeqNo_{0};

private:
   uint32_t ItchSeconds_{0};
};

} // namespaces
#endif//__f9set_ExgMdItchParser_hpp__

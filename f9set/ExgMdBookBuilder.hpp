// \file f9set/ExgMdBookBuilder.hpp
#ifndef __f9set_ExgMdBookBuilder_hpp__
#define __f9set_ExgMdBookBuilder_hpp__
#include "f9set/ExgMdItchParser.hpp"
#include "f9set/ExgMdOrderBook.hpp"

fon9_BEFORE_INCLUDE_STD
#include <memory>
fon9_AFTER_INCLUDE_STD

namespace f9set {

/// 由 ITCH 訊息建立各商品的: 基本資料、委託簿(五檔)、成交、盤別.
/// - 盤別只依 Order Book State 訊息, 不依時間推算(SET 開收盤為隨機結束, 且有動態價格區間).
/// - 成交明細/成交量: 'E' 全部計入; 'C' 及 'P' 只計入 Printable == 'Y' 的訊息, 避免集合競價重複計算.
/// - 'S'(Start of Messages) 時清除每日資料.
/// - 目前不處理: 組合單('M'), 缺號回補(MoldUDP64 re-request), GLIMPSE 快照.
class f9set_API ExgMdBookBuilder : public ExgMdItchParser {
   fon9_NON_COPY_NON_MOVE(ExgMdBookBuilder);
   using base = ExgMdItchParser;
public:
   ExgMdBookBuilder() = default;
   virtual ~ExgMdBookBuilder();

   ExgMdOrderBook* GetOrderBook(OrderBookId id) const {
      auto ifind = this->Books_.find(id);
      return ifind == this->Books_.end() ? nullptr : ifind->second.get();
   }
   /// 找不到則建立一個新的.
   ExgMdOrderBook& FetchOrderBook(OrderBookId id);
   size_t GetOrderBookCount() const {
      return this->Books_.size();
   }

   /// 收到 Execute/Replace/Delete, 但找不到委託的次數.
   /// 可能原因: 盤中啟動(尚未取得快照), 或缺號.
   uint64_t OrderNotFoundCount_{0};

protected:
   void OnItchSystemEvent(const ItchSystemEvent& msg) override;
   void OnItchOrderBookDirectory(const ItchOrderBookDirectory& msg) override;
   void OnItchOrderBookState(const ItchOrderBookState& msg) override;
   void OnItchAddOrder(const ItchAddOrder& msg, const ItchAlpha<7>* participantId) override;
   void OnItchOrderExecuted(const ItchOrderExecuted& msg) override;
   void OnItchOrderExecutedWithPrice(const ItchOrderExecutedWithPrice& msg) override;
   void OnItchOrderReplace(const ItchOrderReplace& msg) override;
   void OnItchOrderDelete(const ItchOrderDelete& msg) override;
   void OnItchTrade(const ItchTrade& msg) override;
   void OnItchEquilibriumPriceUpdate(const ItchEquilibriumPriceUpdate& msg) override;

   // ----- 事件通知, 衍生者可覆寫, 例如: 轉成 fon9 的 MdRts 發行.
   /// 委託簿可能有異動(五檔不一定有變).
   virtual void OnBookChanged(ExgMdOrderBook& book, const ItchMsgHead& msg);
   /// 已更新 book.LastDeal_, TotalQty_, DealCount_ 之後通知.
   virtual void OnDeal(ExgMdOrderBook& book, const ExgMdDeal& deal);
   /// 已更新 book.Phase_ 之後通知.
   virtual void OnPhaseChanged(ExgMdOrderBook& book, SetTradingPhase oldPhase);
   virtual void OnEquilibriumChanged(ExgMdOrderBook& book);
   /// 換日(Start of Messages), 已清除每日資料之後通知.
   virtual void OnStartOfMessages();
   virtual void OnEndOfMessages();

private:
   void UpdateDeal(ExgMdOrderBook& book, const ItchMsgHead& msg, ItchPrice pri, ItchQty qty,
                   const ItchMatchId& matchId, bool isAuction);
   void OnExecuted(const ItchOrderExecuted& msg, const ItchPrice* tradePri, bool isPrintable, bool isAuction);

   using Books = std::unordered_map<OrderBookId, std::unique_ptr<ExgMdOrderBook>>;
   Books Books_;
};

} // namespaces
#endif//__f9set_ExgMdBookBuilder_hpp__

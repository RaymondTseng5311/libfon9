// \file f9set/ExgMdOrderBook.hpp
#ifndef __f9set_ExgMdOrderBook_hpp__
#define __f9set_ExgMdOrderBook_hpp__
#include "f9set/ExgTypes.hpp"
#include "fon9/fmkt/SymbBSData.hpp"
#include "fon9/TimeStamp.hpp"

fon9_BEFORE_INCLUDE_STD
#include <map>
#include <unordered_map>
#include <string>
fon9_AFTER_INCLUDE_STD

namespace f9set {

/// 成交.
struct ExgMdDeal {
   fon9::TimeStamp   Time_;
   ItchPrice         Price_{0};
   ItchQty           Qty_{0};
   uint64_t          MatchNo_{0};
   /// 是否為集合競價成交.
   bool              IsAuction_{false};
};

/// 一個商品(ITCH Order Book) 的委託簿及基本資料.
/// - ITCH 為逐筆委託(Market by Order), 由 Add/Execute/Replace/Delete 自行建立價位;
/// - ITCH Order ID 只在 [OrderBook + Side] 之內唯一, 所以買賣各自一個委託表;
/// - 價格使用 ITCH 原始整數, 需要時透過 ToPri() 依 PriceDecimals_ 轉成 fon9::fmkt::Pri.
class f9set_API ExgMdOrderBook {
   fon9_NON_COPY_NON_MOVE(ExgMdOrderBook);
public:
   const OrderBookId Id_;

   // ----- 基本資料: 來自 Order Book Directory.
   std::string       Symbol_;
   std::string       Isin_;
   uint8_t           FinancialProduct_{0};
   uint16_t          PriceDecimals_{0};
   uint32_t          RoundLotSize_{0};
   uint32_t          OddLotSize_{0};

   // ----- 盤別: 來自 Order Book State.
   SetTradingPhase   Phase_{SetTradingPhase::Unknown};
   /// 原始的 StateName, 當 Phase_ == Unknown 時, 可用來判斷 SET 新增了哪些狀態.
   std::string       StateName_;
   fon9::TimeStamp   PhaseTime_;

   // ----- 成交.
   ExgMdDeal         LastDeal_;
   ItchQty           TotalQty_{0};
   uint64_t          DealCount_{0};

   // ----- 集合競價試算: 來自 Equilibrium Price Update.
   ItchPrice         EqPrice_{0};
   ItchQty           EqBidQty_{0};
   ItchQty           EqAskQty_{0};

   ExgMdOrderBook(OrderBookId id) : Id_{id} {
   }

   fon9::fmkt::Pri ToPri(ItchPrice pri) const {
      fon9::fmkt::Pri retval;
      retval.Assign(pri, static_cast<fon9::DecScaleT>(this->PriceDecimals_));
      return retval;
   }

   /// 新增委託; 若 OrderId 已存在(不應發生), 則先移除舊的委託.
   void AddOrder(ExgSide side, ItchOrderId id, ItchPrice pri, ItchQty qty);
   /// 委託成交, 數量扣減到 0 時移除委託.
   /// 隱藏數量(可見數量為 0) 的委託, 成交後不會移除, 須等 Delete 訊息.
   /// \retval false 找不到委託.
   bool ExecuteOrder(ExgSide side, ItchOrderId id, ItchQty execQty, ItchPrice* orderPri);
   /// 委託異動: 新的價格及可見數量.
   bool ReplaceOrder(ExgSide side, ItchOrderId id, ItchPrice pri, ItchQty qty);
   bool DeleteOrder(ExgSide side, ItchOrderId id);
   /// 清除全部委託, 例如: 換日, 或缺號時等待重建.
   void ClearOrders();
   /// 清除每日資料(成交, 盤別, 試算), 及全部委託; 基本資料保留.
   void ClearDaily();

   size_t GetOrderCount(ExgSide side) const {
      return side == ExgSide::Buy ? this->BuyOrders_.size() : this->SellOrders_.size();
   }
   size_t GetLevelCount(ExgSide side) const {
      return side == ExgSide::Buy ? this->BuyLevels_.size() : this->SellLevels_.size();
   }
   /// 取得最佳 count 檔價量, [0]=最佳價; 不足 count 的部分填 0.
   /// \retval 實際有的檔數.
   unsigned GetLevels(ExgSide side, fon9::fmkt::PriQty* dst, unsigned count) const;
   /// 填入五檔(fon9::fmkt::SymbBSData::kBSCount).
   void GetBS(fon9::fmkt::SymbBSData& dst) const {
      this->GetLevels(ExgSide::Buy, dst.Buys_, fon9::fmkt::SymbBSData::kBSCount);
      this->GetLevels(ExgSide::Sell, dst.Sells_, fon9::fmkt::SymbBSData::kBSCount);
   }

private:
   struct Order {
      ItchPrice   Pri_;
      ItchQty     Qty_;
   };
   using Orders = std::unordered_map<ItchOrderId, Order>;
   using BuyLevels = std::map<ItchPrice, ItchQty, std::greater<ItchPrice>>;
   using SellLevels = std::map<ItchPrice, ItchQty>;
   Orders      BuyOrders_;
   Orders      SellOrders_;
   BuyLevels   BuyLevels_;
   SellLevels  SellLevels_;

   Orders& GetOrders(ExgSide side) {
      return side == ExgSide::Buy ? this->BuyOrders_ : this->SellOrders_;
   }
   void AddLevelQty(ExgSide side, ItchPrice pri, ItchQty qty);
   void SubLevelQty(ExgSide side, ItchPrice pri, ItchQty qty);
};

} // namespaces
#endif//__f9set_ExgMdOrderBook_hpp__

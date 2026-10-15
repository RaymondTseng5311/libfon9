// \file f9set/ExgMdOrderBook.cpp
#include "f9set/ExgMdOrderBook.hpp"

namespace f9set {

template <class Levels>
static void AddLevelQtyT(Levels& levels, ItchPrice pri, ItchQty qty) {
   if (qty)
      levels[pri] += qty;
}
template <class Levels>
static void SubLevelQtyT(Levels& levels, ItchPrice pri, ItchQty qty) {
   if (qty == 0)
      return;
   auto ifind = levels.find(pri);
   if (ifind == levels.end())
      return;
   if (ifind->second <= qty)
      levels.erase(ifind);
   else
      ifind->second -= qty;
}
void ExgMdOrderBook::AddLevelQty(ExgSide side, ItchPrice pri, ItchQty qty) {
   if (side == ExgSide::Buy)
      AddLevelQtyT(this->BuyLevels_, pri, qty);
   else
      AddLevelQtyT(this->SellLevels_, pri, qty);
}
void ExgMdOrderBook::SubLevelQty(ExgSide side, ItchPrice pri, ItchQty qty) {
   if (side == ExgSide::Buy)
      SubLevelQtyT(this->BuyLevels_, pri, qty);
   else
      SubLevelQtyT(this->SellLevels_, pri, qty);
}
//--------------------------------------------------------------------------//
void ExgMdOrderBook::AddOrder(ExgSide side, ItchOrderId id, ItchPrice pri, ItchQty qty) {
   Order& ord = this->GetOrders(side)[id];
   // 若已存在(例如: 重複收到, 或 inactive 委託重新啟用), 先扣除舊的數量.
   this->SubLevelQty(side, ord.Pri_, ord.Qty_);
   ord.Pri_ = pri;
   ord.Qty_ = qty;
   this->AddLevelQty(side, pri, qty);
}
bool ExgMdOrderBook::ExecuteOrder(ExgSide side, ItchOrderId id, ItchQty execQty, ItchPrice* orderPri) {
   Orders& orders = this->GetOrders(side);
   auto    ifind = orders.find(id);
   if (ifind == orders.end())
      return false;
   Order& ord = ifind->second;
   if (orderPri)
      *orderPri = ord.Pri_;
   if (ord.Qty_ == 0) // 隱藏數量委託: 等 Delete 訊息再移除.
      return true;
   if (execQty > ord.Qty_)
      execQty = ord.Qty_;
   this->SubLevelQty(side, ord.Pri_, execQty);
   if ((ord.Qty_ -= execQty) == 0)
      orders.erase(ifind);
   return true;
}
bool ExgMdOrderBook::ReplaceOrder(ExgSide side, ItchOrderId id, ItchPrice pri, ItchQty qty) {
   Orders& orders = this->GetOrders(side);
   auto    ifind = orders.find(id);
   if (ifind == orders.end())
      return false;
   Order& ord = ifind->second;
   this->SubLevelQty(side, ord.Pri_, ord.Qty_);
   ord.Pri_ = pri;
   ord.Qty_ = qty;
   this->AddLevelQty(side, pri, qty);
   return true;
}
bool ExgMdOrderBook::DeleteOrder(ExgSide side, ItchOrderId id) {
   Orders& orders = this->GetOrders(side);
   auto    ifind = orders.find(id);
   if (ifind == orders.end())
      return false;
   this->SubLevelQty(side, ifind->second.Pri_, ifind->second.Qty_);
   orders.erase(ifind);
   return true;
}
void ExgMdOrderBook::ClearOrders() {
   this->BuyOrders_.clear();
   this->SellOrders_.clear();
   this->BuyLevels_.clear();
   this->SellLevels_.clear();
}
void ExgMdOrderBook::ClearDaily() {
   this->ClearOrders();
   this->Phase_ = SetTradingPhase::Unknown;
   this->StateName_.clear();
   this->PhaseTime_.AssignNull();
   this->LastDeal_ = ExgMdDeal{};
   this->TotalQty_ = 0;
   this->DealCount_ = 0;
   this->EqPrice_ = 0;
   this->EqBidQty_ = this->EqAskQty_ = 0;
}
//--------------------------------------------------------------------------//
template <class Levels>
static unsigned GetLevelsT(const ExgMdOrderBook& book, const Levels& levels, fon9::fmkt::PriQty* dst, unsigned count) {
   unsigned idx = 0;
   for (auto i = levels.begin(); idx < count && i != levels.end(); ++i, ++idx) {
      dst[idx].Pri_ = book.ToPri(i->first);
      dst[idx].Qty_ = i->second;
   }
   const unsigned retval = idx;
   for (; idx < count; ++idx)
      dst[idx] = fon9::fmkt::PriQty{};
   return retval;
}
unsigned ExgMdOrderBook::GetLevels(ExgSide side, fon9::fmkt::PriQty* dst, unsigned count) const {
   if (side == ExgSide::Buy)
      return GetLevelsT(*this, this->BuyLevels_, dst, count);
   return GetLevelsT(*this, this->SellLevels_, dst, count);
}

} // namespaces

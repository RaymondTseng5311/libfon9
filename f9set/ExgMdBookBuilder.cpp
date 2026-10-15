// \file f9set/ExgMdBookBuilder.cpp
#include "f9set/ExgMdBookBuilder.hpp"
#include "fon9/StrTo.hpp"
#include "fon9/StrTools.hpp"

namespace f9set {

ExgMdBookBuilder::~ExgMdBookBuilder() {
}
ExgMdOrderBook& ExgMdBookBuilder::FetchOrderBook(OrderBookId id) {
   std::unique_ptr<ExgMdOrderBook>& book = this->Books_[id];
   if (!book)
      book.reset(new ExgMdOrderBook{id});
   return *book;
}

void ExgMdBookBuilder::OnBookChanged(ExgMdOrderBook&, const ItchMsgHead&) {}
void ExgMdBookBuilder::OnDeal(ExgMdOrderBook&, const ExgMdDeal&) {}
void ExgMdBookBuilder::OnPhaseChanged(ExgMdOrderBook&, SetTradingPhase) {}
void ExgMdBookBuilder::OnEquilibriumChanged(ExgMdOrderBook&) {}
void ExgMdBookBuilder::OnStartOfMessages() {}
void ExgMdBookBuilder::OnEndOfMessages() {}
void ExgMdBookBuilder::OnSnapshotDone() {}

void ExgMdBookBuilder::BeginSnapshot() {
   base::BeginSnapshot();
   for (auto& book : this->Books_)
      book.second->ClearOrders();
}
void ExgMdBookBuilder::OnItchEndOfSnapshot(const ItchEndOfSnapshot& msg) {
   fon9::StrView seqstr = ToStrView(msg.SeqNo_);
   fon9::StrTrim(&seqstr);
   this->EndSnapshot(fon9::StrTo(seqstr, uint64_t{0}));
   this->OnSnapshotDone();
}

//--------------------------------------------------------------------------//
void ExgMdBookBuilder::OnItchSystemEvent(const ItchSystemEvent& msg) {
   switch (msg.GetEventCode()) {
   case ItchSystemEventCode::StartOfMessages:
      for (auto& book : this->Books_)
         book.second->ClearDaily();
      this->OnStartOfMessages();
      break;
   case ItchSystemEventCode::EndOfMessages:
      this->OnEndOfMessages();
      break;
   }
}
void ExgMdBookBuilder::OnItchOrderBookDirectory(const ItchOrderBookDirectory& msg) {
   ExgMdOrderBook& book = this->FetchOrderBook(msg.OrderBookId_.Get());
   book.Symbol_ = ToStrView(msg.Symbol_).ToString();
   book.Isin_ = ToStrView(msg.Isin_).ToString();
   book.FinancialProduct_ = msg.FinancialProduct_.Get();
   book.PriceDecimals_ = msg.PriceDecimals_.Get();
   book.RoundLotSize_ = msg.RoundLotSize_.Get();
   book.OddLotSize_ = msg.OddLotSize_.Get();
}
void ExgMdBookBuilder::OnItchOrderBookState(const ItchOrderBookState& msg) {
   ExgMdOrderBook&       book = this->FetchOrderBook(msg.OrderBookId_.Get());
   const fon9::StrView   stateName = ToStrView(msg.StateName_);
   const SetTradingPhase oldPhase = book.Phase_;
   book.StateName_ = stateName.ToString();
   book.Phase_ = ItchStateNameToPhase(stateName);
   book.PhaseTime_ = this->GetItchTime(msg);
   if (oldPhase != book.Phase_)
      this->OnPhaseChanged(book, oldPhase);
}
//--------------------------------------------------------------------------//
void ExgMdBookBuilder::OnItchAddOrder(const ItchAddOrder& msg, const ItchAlpha<7>*) {
   ExgMdOrderBook& book = this->FetchOrderBook(msg.OrderBookId_.Get());
   book.AddOrder(msg.GetSide(), msg.OrderId_.Get(), msg.Price_.Get(), msg.Quantity_.Get());
   this->OnBookChanged(book, msg);
}
void ExgMdBookBuilder::OnItchOrderReplace(const ItchOrderReplace& msg) {
   ExgMdOrderBook& book = this->FetchOrderBook(msg.OrderBookId_.Get());
   if (!book.ReplaceOrder(msg.GetSide(), msg.OrderId_.Get(), msg.Price_.Get(), msg.Quantity_.Get())) {
      ++this->OrderNotFoundCount_;
      return;
   }
   this->OnBookChanged(book, msg);
}
void ExgMdBookBuilder::OnItchOrderDelete(const ItchOrderDelete& msg) {
   ExgMdOrderBook& book = this->FetchOrderBook(msg.OrderBookId_.Get());
   if (!book.DeleteOrder(msg.GetSide(), msg.OrderId_.Get())) {
      ++this->OrderNotFoundCount_;
      return;
   }
   this->OnBookChanged(book, msg);
}
//--------------------------------------------------------------------------//
void ExgMdBookBuilder::UpdateDeal(ExgMdOrderBook& book, const ItchMsgHead& msg, ItchPrice pri, ItchQty qty,
                                  const ItchMatchId& matchId, bool isAuction) {
   book.LastDeal_.Time_ = this->GetItchTime(msg);
   book.LastDeal_.Price_ = pri;
   book.LastDeal_.Qty_ = qty;
   book.LastDeal_.MatchNo_ = matchId.MatchNo_.Get();
   book.LastDeal_.IsAuction_ = isAuction;
   book.TotalQty_ += qty;
   ++book.DealCount_;
   this->OnDeal(book, book.LastDeal_);
}
void ExgMdBookBuilder::OnExecuted(const ItchOrderExecuted& msg, const ItchPrice* tradePri, bool isPrintable, bool isAuction) {
   ExgMdOrderBook& book = this->FetchOrderBook(msg.OrderBookId_.Get());
   ItchPrice       ordPri;
   const ItchQty   execQty = msg.ExecutedQuantity_.Get();
   if (!book.ExecuteOrder(msg.GetSide(), msg.OrderId_.Get(), execQty, &ordPri)) {
      ++this->OrderNotFoundCount_;
      if (tradePri == nullptr) // 'E' 的成交價 = 委託價, 找不到委託就無法得知成交價.
         return;
   }
   else {
      this->OnBookChanged(book, msg);
   }
   if (isPrintable)
      this->UpdateDeal(book, msg, tradePri ? *tradePri : ordPri, execQty, msg.MatchId_, isAuction);
}
void ExgMdBookBuilder::OnItchOrderExecuted(const ItchOrderExecuted& msg) {
   // 'E': 連續撮合時, 委託簿上的委託被新進委託成交; 只有委託簿上那一方會送出 'E', 所以不會重複計算.
   this->OnExecuted(msg, nullptr, true, IsAuctionPhase(this->FetchOrderBook(msg.OrderBookId_.Get()).Phase_));
}
void ExgMdBookBuilder::OnItchOrderExecutedWithPrice(const ItchOrderExecutedWithPrice& msg) {
   const ItchPrice pri = msg.TradePrice_.Get();
   this->OnExecuted(msg, &pri, msg.Printable_ == 'Y', msg.OccurredAtCross_ == 'Y');
}
void ExgMdBookBuilder::OnItchTrade(const ItchTrade& msg) {
   if (msg.Printable_ != 'Y')
      return;
   ExgMdOrderBook& book = this->FetchOrderBook(msg.OrderBookId_.Get());
   this->UpdateDeal(book, msg, msg.TradePrice_.Get(), msg.Quantity_.Get(), msg.MatchId_, msg.OccurredAtCross_ == 'Y');
}
void ExgMdBookBuilder::OnItchEquilibriumPriceUpdate(const ItchEquilibriumPriceUpdate& msg) {
   ExgMdOrderBook& book = this->FetchOrderBook(msg.OrderBookId_.Get());
   book.EqPrice_ = msg.EquilibriumPrice_.Get();
   book.EqBidQty_ = msg.BidQuantity_.Get();
   book.EqAskQty_ = msg.AskQuantity_.Get();
   this->OnEquilibriumChanged(book);
}

} // namespaces

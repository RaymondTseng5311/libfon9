// \file f9set/ExgMdRtsBuilder.cpp
#include "f9set/ExgMdRtsBuilder.hpp"
#include "fon9/fmkt/MdRtsTypes.hpp"
#include "fon9/seed/RawWr.hpp"

namespace f9set {
namespace f9fmkt = fon9::fmkt;

f9set_API f9fmkt_TradingSessionSt ToTradingSessionSt(SetTradingPhase ph) {
   switch (ph) {
   case SetTradingPhase::Unknown:      break;
   case SetTradingPhase::PreTrading:   return f9fmkt_TradingSessionSt_Clear;
   case SetTradingPhase::PreOpen:      return f9fmkt_TradingSessionSt_PreOpen;
   case SetTradingPhase::Open:         return f9fmkt_TradingSessionSt_Open;
   case SetTradingPhase::PreClose:     return f9fmkt_TradingSessionSt_PreClose;
   case SetTradingPhase::OffHour:
   case SetTradingPhase::Closed:       return f9fmkt_TradingSessionSt_Closed;
   // fon9 沒有「午休」, 用 Halted 表示暫停, 之後恢復為 Open/PreOpen.
   case SetTradingPhase::Intermission:
   case SetTradingPhase::Pause:
   case SetTradingPhase::Halt:
   case SetTradingPhase::Suspend:      return f9fmkt_TradingSessionSt_Halted;
   }
   return f9fmkt_TradingSessionSt_Unknown;
}
static f9fmkt::TwsBaseFlag ToMatchingMethod(SetTradingPhase ph) {
   return IsAuctionPhase(ph) ? f9fmkt::TwsBaseFlag::MatchingMethod_AggregateAuction
                             : f9fmkt::TwsBaseFlag::MatchingMethod_ContinuousMarket;
}
//--------------------------------------------------------------------------//
ExgMdRtsBuilder::ExgMdRtsBuilder(ExgMdSymbsSP symbs) : Symbs_{std::move(symbs)} {
}
ExgMdRtsBuilder::~ExgMdRtsBuilder() {
}
ExgMdSymb* ExgMdRtsBuilder::GetSymb(OrderBookId id) const {
   auto ifind = this->SymbRecs_.find(id);
   return ifind == this->SymbRecs_.end() ? nullptr : ifind->second.Symb_.get();
}
//--------------------------------------------------------------------------//
void ExgMdRtsBuilder::OnItchOrderBookDirectory(const ItchOrderBookDirectory& msg) {
   base::OnItchOrderBookDirectory(msg);
   const OrderBookId id = msg.OrderBookId_.Get();
   SymbRec&          rec = this->SymbRecs_[id];
   rec.Book_ = this->GetOrderBook(id);
   auto              symbsLk = this->Symbs_->SymbMap_.Lock();
   rec.Symb_ = fon9::static_pointer_cast<ExgMdSymb>(this->Symbs_->FetchSymb(symbsLk, ToStrView(msg.Symbol_)));
   ExgMdSymb&        symb = *rec.Symb_;
   symb.OrderBookId_ = id;
   symb.PriceDecimals_ = msg.PriceDecimals_.Get();
   const uint32_t    shUnit = msg.RoundLotSize_.Get();
   if (symb.ShUnit_ != shUnit || symb.TradingMarket_ != f9fmkt_TradingMarket_SET) {
      symb.ShUnit_ = shUnit;
      symb.TradingMarket_ = f9fmkt_TradingMarket_SET;
      // BaseInfoTw: TradingMarket, ShUnit, PriRef, 漲跌停(沒有).
      fon9::RevBufferList rbuf{64};
      fon9::ToBitv(rbuf, symb.Ref_.Data_.PriRef_);
      fon9::ToBitv(rbuf, symb.ShUnit_);
      *rbuf.AllocPacket<char>() = symb.TradingMarket_;
      symb.MdRtStream_.Publish(ToStrView(symb.SymbId_), f9sv_RtsPackType_BaseInfoTw,
                               f9sv_MdRtsKind_Base | f9sv_MdRtsKind_Ref,
                               fon9::DayTime::Null(), std::move(rbuf));
   }
   fon9::StrView name = ToStrView(msg.LongName_);
   if (name.empty())
      name = ToStrView(msg.Symbol_);
   if (ToStrView(symb.NameUTF8_) != name) {
      symb.NameUTF8_.AssignFrom(name);
      fon9::RevBufferList rbuf{64};
      f9fmkt::MdRtsPackFieldValue(rbuf, *this->Symbs_->FldBaseNameUTF8_, fon9::seed::SimpleRawRd{symb});
      symb.MdRtStream_.PublishAndSave(ToStrView(symb.SymbId_), f9sv_RtsPackType_FieldValue_NoInfoTime,
                                      f9sv_MdRtsKind_Base | f9sv_MdRtsKind_NoInfoTime, std::move(rbuf));
   }
}
//--------------------------------------------------------------------------//
void ExgMdRtsBuilder::OnBookChanged(ExgMdOrderBook& book, const ItchMsgHead& msg) {
   SymbRec* rec = this->GetSymbRec(book.Id_);
   if (rec == nullptr || !rec->Symb_) {
      ++this->NoSymbCount_;
      return;
   }
   rec->Book_ = &book;
   rec->BSTime_ = this->GetItchTime(msg);
   rec->BSSeqNo_ = this->CurSeqNo_;
   this->SetBSDirty(*rec);
}
void ExgMdRtsBuilder::OnStartOfMessages() {
   // 委託簿已清除, 若之前有五檔, 則需要發行清除後的五檔.
   for (auto& irec : this->SymbRecs_) {
      irec.second.BSTime_.AssignNull();
      if (irec.second.Book_)
         this->SetBSDirty(irec.second);
   }
}
void ExgMdRtsBuilder::OnSnapshotDone() {
   for (auto& irec : this->SymbRecs_) {
      if (irec.second.Book_)
         this->SetBSDirty(irec.second);
   }
   this->FlushBS();
}
void ExgMdRtsBuilder::FlushBS() {
   if (this->DirtyBS_.empty() || this->IsWaitingSnapshot())
      return;
   auto symbsLk = this->Symbs_->SymbMap_.Lock();
   for (SymbRec* rec : this->DirtyBS_) {
      rec->IsBSDirty_ = false;
      this->PublishBS(*rec);
   }
   this->DirtyBS_.clear();
}
void ExgMdRtsBuilder::PublishBS(SymbRec& rec) {
   ExgMdSymb&           symb = *rec.Symb_;
   const ExgMdOrderBook& book = *rec.Book_;
   f9fmkt::SymbBSData&  dst = symb.BS_.Data_;
   f9fmkt::PriQty       buys[f9fmkt::SymbBSData::kBSCount];
   f9fmkt::PriQty       sells[f9fmkt::SymbBSData::kBSCount];
   book.GetLevels(ExgSide::Buy, buys, f9fmkt::SymbBSData::kBSCount);
   book.GetLevels(ExgSide::Sell, sells, f9fmkt::SymbBSData::kBSCount);
   if (memcmp(buys, dst.Buys_, sizeof(buys)) == 0 && memcmp(sells, dst.Sells_, sizeof(sells)) == 0)
      return;
   memcpy(dst.Buys_, buys, sizeof(buys));
   memcpy(dst.Sells_, sells, sizeof(sells));
   dst.InfoTime_ = this->ToDayTime(rec.BSTime_);
   dst.Flags_ = f9sv_BSFlag_OrderBuy | f9sv_BSFlag_OrderSell;
   fon9::RevBufferList rts{128};
   f9fmkt::MdRtsPackSnapshotBS(rts, dst);
   f9fmkt::PackMktSeq(rts, dst.MarketSeq_, this->Symbs_->CtrlFlags_,
                      static_cast<f9fmkt::MarketDataSeq>(rec.BSSeqNo_));
   symb.MdRtStream_.Publish(ToStrView(symb.SymbId_), f9sv_RtsPackType_SnapshotBS, f9sv_MdRtsKind_BS,
                            dst.InfoTime_, std::move(rts));
   ++this->PubBSCount_;
}
//--------------------------------------------------------------------------//
void ExgMdRtsBuilder::OnDeal(ExgMdOrderBook& book, const ExgMdDeal& deal) {
   SymbRec* rec = this->GetSymbRec(book.Id_);
   if (rec == nullptr || !rec->Symb_) {
      ++this->NoSymbCount_;
      return;
   }
   ExgMdSymb&           symb = *rec->Symb_;
   const fon9::DayTime  dealTime = this->ToDayTime(deal.Time_);
   auto                 symbsLk = this->Symbs_->SymbMap_.Lock();
   f9fmkt::SymbDealData& dst = symb.Deal_.Data_;
   dst.Flags_ = f9sv_DealFlag{};
   if (dst.InfoTime_ != dealTime) {
      dst.DealTime_ = dst.InfoTime_ = dealTime;
      dst.Flags_ |= f9sv_DealFlag_DealTimeChanged;
   }
   dst.Deal_.Pri_ = book.ToPri(deal.Price_);
   dst.Deal_.Qty_ = deal.Qty_;
   // book.TotalQty_ 已包含此筆成交; 若之前有遺漏(缺號), 則告知訂閱者.
   if (dst.TotalQty_ + deal.Qty_ != book.TotalQty_)
      dst.Flags_ |= f9sv_DealFlag_TotalQtyLost;
   dst.TotalQty_ = book.TotalQty_;
   if (deal.Qty_ > 0)
      symb.CheckOHL(dst.Deal_.Pri_, dealTime);

   // DealPack: 格式同 f9tws::FmtRt_PkContOnReceived().
   fon9::RevBufferList rts{64};
   fon9::ToBitv(rts, dst.Deal_.Qty_);
   fon9::ToBitv(rts, dst.Deal_.Pri_);
   *rts.AllocPacket<uint8_t>() = 0; // = 1筆成交.
   if (IsEnumContains(dst.Flags_, f9sv_DealFlag_TotalQtyLost))
      fon9::ToBitv(rts, dst.TotalQty_ - dst.Deal_.Qty_);
   if (IsEnumContains(dst.Flags_, f9sv_DealFlag_DealTimeChanged))
      fon9::RevPutBitv(rts, fon9_BitvV_NumberNull);
   *rts.AllocPacket<uint8_t>() = fon9::cast_to_underlying(dst.Flags_);
   f9fmkt::PackMktSeq(rts, dst.MarketSeq_, this->Symbs_->CtrlFlags_,
                      static_cast<f9fmkt::MarketDataSeq>(this->CurSeqNo_));
   symb.MdRtStream_.Publish(ToStrView(symb.SymbId_), f9sv_RtsPackType_DealPack, f9sv_MdRtsKind_Deal,
                            dealTime, std::move(rts));
   ++this->PubDealCount_;
}
//--------------------------------------------------------------------------//
void ExgMdRtsBuilder::OnPhaseChanged(ExgMdOrderBook& book, SetTradingPhase oldPhase) {
   (void)oldPhase;
   SymbRec* rec = this->GetSymbRec(book.Id_);
   if (rec == nullptr || !rec->Symb_) {
      ++this->NoSymbCount_;
      return;
   }
   auto symbsLk = this->Symbs_->SymbMap_.Lock();
   this->PublishSessionSt(*rec->Symb_, book.Phase_, this->ToDayTime(book.PhaseTime_));
}
void ExgMdRtsBuilder::PublishSessionSt(ExgMdSymb& symb, SetTradingPhase ph, fon9::DayTime infoTime) {
   const f9fmkt_TradingSessionSt sessionSt = ToTradingSessionSt(ph);
   const f9fmkt::TwsBaseFlag     matchingMethod = ToMatchingMethod(ph);
   if (symb.TradingSessionSt_ == sessionSt
       && symb.TradingSessionId_ == f9fmkt_TradingSessionId_Normal
       && f9fmkt::GetMatchingMethod(symb.TwsFlags_) == matchingMethod)
      return;
   fon9::RevBufferList     rts{64};
   fon9::seed::SimpleRawRd rd{symb};
   if (symb.TradingSessionId_ != f9fmkt_TradingSessionId_Normal) {
      symb.TradingSessionId_ = f9fmkt_TradingSessionId_Normal;
      f9fmkt::MdRtsPackFieldValue(rts, *this->Symbs_->FldBaseSession_, rd);
   }
   if (f9fmkt::GetMatchingMethod(symb.TwsFlags_) != matchingMethod) {
      f9fmkt::SetMatchingMethod(&symb.TwsFlags_, matchingMethod);
      f9fmkt::MdRtsPackFieldValue(rts, *this->Symbs_->FldBaseTwsFlags_, rd);
   }
   if (symb.TradingSessionSt_ != sessionSt) {
      symb.TradingSessionSt_ = sessionSt;
      f9fmkt::MdRtsPackFieldValue(rts, *this->Symbs_->FldBaseSessionSt_, rd);
   }
   symb.MdRtStream_.Publish(ToStrView(symb.SymbId_), f9sv_RtsPackType_FieldValue_AndInfoTime,
                            f9sv_MdRtsKind_TradingSession, infoTime, std::move(rts));
   ++this->PubSessionStCount_;
}

} // namespaces

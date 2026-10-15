// \file f9set/ExgMdSymbs.cpp
#include "f9set/ExgMdSymbs.hpp"
#include "fon9/seed/FieldMaker.hpp"
#include "fon9/fmkt/SymbTabNames.h"

namespace f9set {
using namespace fon9;
using namespace fon9::seed;
using namespace fon9::fmkt;

LayoutSP ExgMdSymb::MakeLayout(bool isAddMarketSeq) {
   constexpr auto kTabFlag = TabFlag::NoSapling_NoSeedCommand_Writable;
   auto symbBaseFields = MakeFields();
   symbBaseFields.Add(fon9_MakeField2(ExgMdSymb, NameUTF8));
   symbBaseFields.Add(fon9_MakeField2(ExgMdSymb, OrderBookId));
   symbBaseFields.Add(fon9_MakeField2(ExgMdSymb, PriceDecimals));
   return LayoutSP{new LayoutN(
      fon9_MakeField(Symb, SymbId_, "Id"), TreeFlag::AddableRemovable | TreeFlag::Unordered,
      TabSP{new Tab{Named{fon9_kCSTR_TabName_Base}, std::move(symbBaseFields), kTabFlag}},
      TabSP{new Tab{Named{fon9_kCSTR_TabName_Ref},  SymbRef_MakeFields(), kTabFlag}},
      TabSP{new Tab{Named{fon9_kCSTR_TabName_Deal}, SymbTwsDeal_MakeFields(isAddMarketSeq), kTabFlag}},
      TabSP{new Tab{Named{fon9_kCSTR_TabName_BS},   SymbTwsBS_MakeFields(isAddMarketSeq), kTabFlag}},
      f9fmkt_MAKE_TABS_OpenHighLow(),
      TabSP{new Tab{Named{fon9_kCSTR_TabName_Rt},   MdRtStream::MakeFields(), kTabFlag}}
   )};
}
static const int32_t kExgMdSymbOffset[]{
   0, // Base
   fon9_OffsetOf(ExgMdSymb, Ref_),
   fon9_OffsetOf(ExgMdSymb, Deal_),
   fon9_OffsetOf(ExgMdSymb, BS_),
   f9fmkt_MAKE_OFFSET_OpenHighLow(ExgMdSymb),
   fon9_OffsetOf(ExgMdSymb, MdRtStream_),
};
static inline SymbData* GetExgMdSymbData(ExgMdSymb* pthis, int tabid) {
   return static_cast<size_t>(tabid) < numofele(kExgMdSymbOffset)
      ? reinterpret_cast<SymbData*>(reinterpret_cast<char*>(pthis) + kExgMdSymbOffset[tabid])
      : nullptr;
}
SymbData* ExgMdSymb::GetSymbData(int tabid) {
   return GetExgMdSymbData(this, tabid);
}
SymbData* ExgMdSymb::FetchSymbData(int tabid) {
   return GetExgMdSymbData(this, tabid);
}
//--------------------------------------------------------------------------//
ExgMdSymb::~ExgMdSymb() {
}
void ExgMdSymb::SessionClear(SymbTree& owner, f9fmkt_TradingSessionId tsesId) {
   this->TradingSessionId_ = tsesId;
   this->TradingSessionSt_ = f9fmkt_TradingSessionSt_Clear;
   this->MdRtStream_.OnSymbSessionClear(owner, *this);
}
void ExgMdSymb::DailyClear(SymbTree& owner, unsigned tdayYYYYMMDD) {
   // 基本資料保留到新的 Order Book Directory 送來.
   const auto     name = this->NameUTF8_;
   const auto     bookId = this->OrderBookId_;
   const auto     decimals = this->PriceDecimals_;
   base::DailyClear(owner, tdayYYYYMMDD);
   this->NameUTF8_ = name;
   this->OrderBookId_ = bookId;
   this->PriceDecimals_ = decimals;
   this->TradingMarket_ = f9fmkt_TradingMarket_SET;
}
void ExgMdSymb::OnBeforeRemove(SymbTree& owner, unsigned tdayYYYYMMDD) {
   (void)tdayYYYYMMDD;
   this->MdRtStream_.BeforeRemove(owner, *this);
}
//--------------------------------------------------------------------------//
ExgMdSymbs::ExgMdSymbs(std::string rtiPathFmt, bool isAddMarketSeq)
   : base(ExgMdSymb::MakeLayout(isAddMarketSeq), std::move(rtiPathFmt),
          MdSymbsCtrlFlag::AllowSubrSnapshotSymb
          | (isAddMarketSeq ? MdSymbsCtrlFlag::HasMarketDataSeq : MdSymbsCtrlFlag{}))
   , TabBase_{LayoutSP_->GetTab(fon9_kCSTR_TabName_Base)}
   , FldBaseNameUTF8_{TabBase_->Fields_.Get("NameUTF8")}
   , FldBaseSession_{TabBase_->Fields_.Get("Session")}
   , FldBaseSessionSt_{TabBase_->Fields_.Get("SessionSt")}
   , FldBaseTwsFlags_{TabBase_->Fields_.Get("TwsFlags")} {
}
SymbSP ExgMdSymbs::MakeSymb(const StrView& symbid) {
   return new ExgMdSymb(symbid, this->RtInnMgr_);
}

} // namespaces

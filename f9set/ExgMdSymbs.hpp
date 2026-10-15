// \file f9set/ExgMdSymbs.hpp
//
// SET 行情商品資料表: 提供給 fon9 MdRts 發行.
//
#ifndef __f9set_ExgMdSymbs_hpp__
#define __f9set_ExgMdSymbs_hpp__
#include "f9set/ExgTypes.hpp"
#include "fon9/fmkt/MdSymbs.hpp"
#include "fon9/fmkt/SymbTws.hpp"
#include "fon9/fmkt/SymbRef.hpp"
#include "fon9/fmkt/SymbBS.hpp"
#include "fon9/fmkt/SymbDeal.hpp"
#include "fon9/fmkt/SymbTimePri.hpp"
#include "fon9/fmkt/MdRtStream.hpp"

namespace f9set {

/// SET 的交易市場代碼(fon9 沒有內建).
constexpr f9fmkt_TradingMarket f9fmkt_TradingMarket_SET = static_cast<f9fmkt_TradingMarket>('S');

using SetNameUTF8 = fon9::CharAry<64>;

/// SET 商品:
/// - SymbId = ITCH Order Book Directory 的 Symbol(例: PTT, PTT-F, PTT-R);
/// - ShUnit = Round Lot Size(股數); 成交量、委託量的單位都是「股」;
/// - TwsFlags 的撮合方式: 集合競價(Pre-open/Pre-close) 或 逐筆撮合, 依 Order Book State 設定.
class f9set_API ExgMdSymb : public fon9::fmkt::SymbTws, public fon9::fmkt::SymbDataOHL {
   fon9_NON_COPY_NON_MOVE(ExgMdSymb);
   using base = fon9::fmkt::SymbTws;
public:
   /// ITCH Order Book Directory 的 Long Name.
   SetNameUTF8             NameUTF8_{nullptr};
   /// ITCH Order Book ID.
   OrderBookId             OrderBookId_{0};
   /// 價格小數位數(來自 ITCH Order Book Directory), 僅供參考; 發行的價格已轉成 fon9::fmkt::Pri.
   uint16_t                PriceDecimals_{0};
   char                    Padding____[2];

   fon9::fmkt::SymbRef     Ref_;
   fon9::fmkt::SymbTwsBS   BS_;
   fon9::fmkt::SymbDeal    Deal_;
   fon9::fmkt::MdRtStream  MdRtStream_;

   ExgMdSymb(const fon9::StrView& symbid, fon9::fmkt::MdRtStreamInnMgr& innMgr)
      : base{symbid}
      , MdRtStream_{innMgr} {
      this->TDayYYYYMMDD_ = innMgr.TDayYYYYMMDD();
      this->TradingMarket_ = f9fmkt_TradingMarket_SET;
      this->MdRtStream_.OpenRtStorage(*this);
   }
   ~ExgMdSymb();

   fon9::fmkt::SymbData* GetSymbData(int tabid) override;
   fon9::fmkt::SymbData* FetchSymbData(int tabid) override;
   void SessionClear(fon9::fmkt::SymbTree& owner, f9fmkt_TradingSessionId tsesId) override;
   void DailyClear(fon9::fmkt::SymbTree& owner, unsigned tdayYYYYMMDD) override;
   void OnBeforeRemove(fon9::fmkt::SymbTree& owner, unsigned tdayYYYYMMDD) override;

   static fon9::seed::LayoutSP MakeLayout(bool isAddMarketSeq);
};
using ExgMdSymbSP = fon9::intrusive_ptr<ExgMdSymb>;

//--------------------------------------------------------------------------//
class f9set_API ExgMdSymbs : public fon9::fmkt::MdSymbsT<ExgMdSymb> {
   fon9_NON_COPY_NON_MOVE(ExgMdSymbs);
   using base = fon9::fmkt::MdSymbsT<ExgMdSymb>;
public:
   const fon9::seed::Tab* const     TabBase_;
   const fon9::seed::Field* const   FldBaseNameUTF8_;
   const fon9::seed::Field* const   FldBaseSession_;
   const fon9::seed::Field* const   FldBaseSessionSt_;
   const fon9::seed::Field* const   FldBaseTwsFlags_;

   /// isAddMarketSeq: 是否在 Deal/BS 加上 MarketSeq 欄位(= MoldUDP64 訊息序號).
   ExgMdSymbs(std::string rtiPathFmt, bool isAddMarketSeq);

   fon9::fmkt::SymbSP MakeSymb(const fon9::StrView& symbid) override;
};
using ExgMdSymbsSP = fon9::intrusive_ptr<ExgMdSymbs>;

} // namespaces
#endif//__f9set_ExgMdSymbs_hpp__

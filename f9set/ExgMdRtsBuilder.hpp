// \file f9set/ExgMdRtsBuilder.hpp
//
// ITCH 委託簿 => fon9 商品資料表(ExgMdSymbs) 及 MdRts 即時發行.
//
#ifndef __f9set_ExgMdRtsBuilder_hpp__
#define __f9set_ExgMdRtsBuilder_hpp__
#include "f9set/ExgMdBookBuilder.hpp"
#include "f9set/ExgMdSymbs.hpp"

fon9_BEFORE_INCLUDE_STD
#include <vector>
fon9_AFTER_INCLUDE_STD

namespace f9set {

/// 盤別 => fon9 的 TradingSessionSt.
f9set_API f9fmkt_TradingSessionSt ToTradingSessionSt(SetTradingPhase ph);

/// 將 ExgMdBookBuilder 的結果寫入 ExgMdSymbs, 並透過 ExgMdSymb::MdRtStream_ 發行:
/// - Order Book Directory: 建立商品(SymbId = Symbol), 設定 ShUnit(RoundLotSize), NameUTF8(LongName),
///   發行 BaseInfoTw(TradingMarket='S', ShUnit) 及 NameUTF8;
/// - 委託簿: 五檔有變時發行 SnapshotBS; 為了減少發行次數, 異動先記下來, 由 FlushBS() 一次發行;
///   (ExgMdSystem 每處理完一個 MoldUDP64 封包就呼叫 FlushBS())
///   等待 GLIMPSE 快照期間不發行五檔, 快照完成後才發行有變動的五檔.
/// - 成交: 發行 DealPack(格式同 f9tws Fmt6), 更新 Open/High/Low;
/// - 盤別: 發行 Base 的 Session/SessionSt/TwsFlags(撮合方式) 欄位.
/// - MarketSeq = MoldUDP64 的訊息序號(fon9 MarketDataSeq 為 32 bits, 一日的訊息量不會超過).
/// - 時間: ITCH 為 UTC, 發行的 DayTime 為交易所當地時間(TimeZoneOffset_, 預設 UTC+7).
/// - 本身不加鎖, 由使用者確保 thread safe; 更新商品資料時會鎖定 Symbs_->SymbMap_.
class f9set_API ExgMdRtsBuilder : public ExgMdBookBuilder {
   fon9_NON_COPY_NON_MOVE(ExgMdRtsBuilder);
   using base = ExgMdBookBuilder;
public:
   const ExgMdSymbsSP   Symbs_;
   /// 交易所時區, 預設 UTC+7(泰國).
   fon9::TimeInterval   TimeZoneOffset_{fon9::TimeInterval_Hour(7)};

   ExgMdRtsBuilder(ExgMdSymbsSP symbs);
   virtual ~ExgMdRtsBuilder();

   /// 發行有異動的五檔.
   void FlushBS();
   /// 由 OrderBookId 取得商品, 若尚未收到 Order Book Directory 則傳回 nullptr.
   ExgMdSymb* GetSymb(OrderBookId id) const;

   fon9::DayTime ToDayTime(fon9::TimeStamp utc) const {
      return utc.IsNull() ? fon9::DayTime::Null() : fon9::GetDayTime(utc + this->TimeZoneOffset_);
   }

   uint64_t PubBSCount_{0};
   uint64_t PubDealCount_{0};
   uint64_t PubSessionStCount_{0};
   /// 收到委託簿異動, 但尚未收到 Order Book Directory.
   uint64_t NoSymbCount_{0};

protected:
   void OnItchOrderBookDirectory(const ItchOrderBookDirectory& msg) override;
   void OnBookChanged(ExgMdOrderBook& book, const ItchMsgHead& msg) override;
   void OnDeal(ExgMdOrderBook& book, const ExgMdDeal& deal) override;
   void OnPhaseChanged(ExgMdOrderBook& book, SetTradingPhase oldPhase) override;
   void OnStartOfMessages() override;
   void OnSnapshotDone() override;

private:
   struct SymbRec {
      ExgMdSymbSP       Symb_;
      ExgMdOrderBook*   Book_{nullptr};
      fon9::TimeStamp   BSTime_;
      uint64_t          BSSeqNo_{0};
      bool              IsBSDirty_{false};
   };
   using SymbRecs = std::unordered_map<OrderBookId, SymbRec>;
   SymbRecs              SymbRecs_;
   std::vector<SymbRec*> DirtyBS_;

   SymbRec* GetSymbRec(OrderBookId id) {
      auto ifind = this->SymbRecs_.find(id);
      return ifind == this->SymbRecs_.end() ? nullptr : &ifind->second;
   }
   void SetBSDirty(SymbRec& rec) {
      if (!rec.IsBSDirty_) {
         rec.IsBSDirty_ = true;
         this->DirtyBS_.push_back(&rec);
      }
   }
   void PublishBS(SymbRec& rec);
   void PublishSessionSt(ExgMdSymb& symb, SetTradingPhase ph, fon9::DayTime infoTime);
};

} // namespaces
#endif//__f9set_ExgMdRtsBuilder_hpp__

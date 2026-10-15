// \file f9set/ExgMdItchFmt.hpp
//
// SET ITCH 行情訊息格式「欄位定義表」.
//
// SET 自 2023-05-08 起使用 Nasdaq Genium INET 架構, 行情為 ITCH over MoldUDP64.
// SET 版 ITCH 規格尚未取得(只提供給會員券商), 目前的欄位定義依據同架構的公開規格:
// - ASX Trade ITCH Message Specification v3.4 (2026/05), Section 2 (ASX ITCH 為 Genium INET ITCH).
// - Nasdaq Genium INET ITCH Multicast (Nordics) v2.1: MoldUDP64 傳輸方式.
//
// 取得 SET ITCH 規格後, 原則上「只需要修改本檔」:
// - 訊息代碼 ItchMsgType.
// - 各訊息的欄位與長度 (每個 struct 都有 static_assert 檢查長度).
// - 盤別名稱對照表 kItchStateNameMap.
// 解析與 Order Book 邏輯(ExgMdItchParser, ExgMdBookBuilder) 只透過本檔的欄位名稱存取.
//
#ifndef __f9set_ExgMdItchFmt_hpp__
#define __f9set_ExgMdItchFmt_hpp__
#include "f9set/ExgTypes.hpp"
#include "fon9/CharAry.hpp"
#include "fon9/Endian.hpp"

namespace f9set {
fon9_PACK(1);

/// ITCH 的數值欄位: unsigned big-endian binary.
template <typename T>
struct ItchNum {
   fon9::byte  Bytes_[sizeof(T)];
   T Get() const {
      return fon9::GetBigEndian<T>(this->Bytes_);
   }
   void Put(T v) {
      fon9::PutBigEndian(this->Bytes_, v);
   }
};
/// ITCH 的價格欄位: signed big-endian, 小數位數由 Order Book Directory 決定.
struct ItchPri {
   ItchNum<uint32_t> Raw_;
   ItchPrice Get() const {
      return static_cast<ItchPrice>(this->Raw_.Get());
   }
   void Put(ItchPrice v) {
      this->Raw_.Put(static_cast<uint32_t>(v));
   }
};
/// ITCH 的文字欄位: 左靠右補空白; ToStrView() 時移除尾端空白.
template <size_t N>
using ItchAlpha = fon9::CharAryP<N, 0, char, ' '>;

/// 價格欄位為此值時, 表示「無價格」, 例如: Equilibrium Price Update 在開盤後送出的清除訊息.
static constexpr ItchPrice kItchNullPrice = static_cast<ItchPrice>(0x80000000u);

/// Genium INET 的 Match ID: 12 bytes = Match Number(8) + Combo Group ID(4).
struct ItchMatchId {
   ItchNum<uint64_t> MatchNo_;
   ItchNum<uint32_t> ComboGroupId_;
};
static_assert(sizeof(ItchMatchId) == 12, "ItchMatchId 沒有 pack?");

//--------------------------------------------------------------------------//

enum class ItchMsgType : char {
   Seconds = 'T',
   OrderBookDirectory = 'R',
   CombinationOrderBookDirectory = 'M',
   TickSize = 'L',
   SystemEvent = 'S',
   OrderBookState = 'O',
   AddOrder = 'A',
   AddOrderMpid = 'F',
   OrderExecuted = 'E',
   OrderExecutedWithPrice = 'C',
   OrderReplace = 'U',
   OrderDelete = 'D',
   Trade = 'P',
   EquilibriumPriceUpdate = 'Z',
   /// GLIMPSE 快照結束.
   EndOfSnapshot = 'G',
};

/// 除了 Seconds 之外, 所有訊息都有 [MsgType + Nanoseconds] 開頭.
struct ItchMsgHead {
   char              MsgType_;
   /// 距離最近一次 Seconds 訊息的奈秒數.
   ItchNum<uint32_t> Nanoseconds_;

   ItchMsgType GetMsgType() const { return static_cast<ItchMsgType>(this->MsgType_); }
};
static_assert(sizeof(ItchMsgHead) == 5, "ItchMsgHead 沒有 pack?");

/// 'T': 每秒(有其他訊息時)送出一次, Unix time (UTC).
struct ItchSeconds {
   char              MsgType_;
   ItchNum<uint32_t> Seconds_;
};
static_assert(sizeof(ItchSeconds) == 5, "ItchSeconds 沒有 pack?");

/// 'R': 商品基本資料, 每日開始時送出全部商品; 盤中新增或異動也會送出.
struct ItchOrderBookDirectory : public ItchMsgHead {
   ItchNum<uint32_t> OrderBookId_;
   ItchAlpha<32>     Symbol_;
   ItchAlpha<32>     LongName_;
   ItchAlpha<12>     Isin_;
   /// 1=Option(含 Warrant/DW?); 3=Future; 5=Cash; 11=Standard combination.
   ItchNum<uint8_t>  FinancialProduct_;
   ItchAlpha<3>      TradingCurrency_;
   ItchNum<uint16_t> PriceDecimals_;
   ItchNum<uint16_t> NominalValueDecimals_;
   ItchNum<uint32_t> OddLotSize_;
   ItchNum<uint32_t> RoundLotSize_;
   ItchNum<uint32_t> BlockLotSize_;
   ItchNum<uint64_t> NominalValue_;
};
static_assert(sizeof(ItchOrderBookDirectory) == 113, "ItchOrderBookDirectory 沒有 pack?");

/// 'L': 檔位表的一個區間; 同一個 OrderBookId 的全部 'L' 組成完整的檔位表.
struct ItchTickSize : public ItchMsgHead {
   ItchNum<uint32_t> OrderBookId_;
   ItchNum<uint64_t> TickSize_;
   ItchPri           PriceFrom_;
   ItchPri           PriceTo_;
};
static_assert(sizeof(ItchTickSize) == 25, "ItchTickSize 沒有 pack?");

enum class ItchSystemEventCode : char {
   StartOfMessages = 'O',
   EndOfMessages = 'C',
};
/// 'S'
struct ItchSystemEvent : public ItchMsgHead {
   char  EventCode_;
   ItchSystemEventCode GetEventCode() const { return static_cast<ItchSystemEventCode>(this->EventCode_); }
};
static_assert(sizeof(ItchSystemEvent) == 6, "ItchSystemEvent 沒有 pack?");

/// 'O': 個股盤別(Session state)變更.
struct ItchOrderBookState : public ItchMsgHead {
   ItchNum<uint32_t> OrderBookId_;
   ItchAlpha<20>     StateName_;
};
static_assert(sizeof(ItchOrderBookState) == 29, "ItchOrderBookState 沒有 pack?");

/// 'A': 新增委託(匿名).
struct ItchAddOrder : public ItchMsgHead {
   ItchNum<uint64_t> OrderId_;
   ItchNum<uint32_t> OrderBookId_;
   char              Side_;
   /// 在同價位(同 OrderBook + Side)中的排序, 1 = 最優先.
   ItchNum<uint32_t> OrderBookPosition_;
   /// 可見數量, 隱藏數量委託為 0.
   ItchNum<uint64_t> Quantity_;
   ItchPri           Price_;
   /// bitmap: 4=Market Bid; 8=Price Stabilisation; 32=Undisclosed; 8192=Implied.
   ItchNum<uint16_t> ExchangeOrderType_;
   /// 0=Undefined; 1=Odd Lot; 2=Round Lot; 3=Block Lot; 4=All or None.
   ItchNum<uint8_t>  LotType_;

   ExgSide GetSide() const { return static_cast<ExgSide>(this->Side_); }
};
static_assert(sizeof(ItchAddOrder) == 37, "ItchAddOrder 沒有 pack?");

/// 'F': 新增委託(含券商代號).
struct ItchAddOrderMpid : public ItchAddOrder {
   ItchAlpha<7>      ParticipantId_;
};
static_assert(sizeof(ItchAddOrderMpid) == 44, "ItchAddOrderMpid 沒有 pack?");

/// 'E': 委託簿上的委託成交(成交價 = 該委託的價格).
struct ItchOrderExecuted : public ItchMsgHead {
   ItchNum<uint64_t> OrderId_;
   ItchNum<uint32_t> OrderBookId_;
   char              Side_;
   ItchNum<uint64_t> ExecutedQuantity_;
   ItchMatchId       MatchId_;
   ItchAlpha<7>      OwnerParticipantId_;
   ItchAlpha<7>      CounterpartyParticipantId_;

   ExgSide GetSide() const { return static_cast<ExgSide>(this->Side_); }
};
static_assert(sizeof(ItchOrderExecuted) == 52, "ItchOrderExecuted 沒有 pack?");

/// 'C': 委託簿上的委託成交, 成交價與委託價不同(例如: 集合競價).
struct ItchOrderExecutedWithPrice : public ItchOrderExecuted {
   ItchPri  TradePrice_;
   /// Y = 集合競價成交; N = 連續撮合成交.
   char     OccurredAtCross_;
   /// N = 不可計入成交明細及成交量(避免重複計算).
   char     Printable_;
};
static_assert(sizeof(ItchOrderExecutedWithPrice) == 58, "ItchOrderExecutedWithPrice 沒有 pack?");

/// 'U': 委託異動, OrderId 不變; 須移到新的 OrderBookPosition.
struct ItchOrderReplace : public ItchMsgHead {
   ItchNum<uint64_t> OrderId_;
   ItchNum<uint32_t> OrderBookId_;
   char              Side_;
   ItchNum<uint32_t> NewOrderBookPosition_;
   ItchNum<uint64_t> Quantity_;
   ItchPri           Price_;
   ItchNum<uint16_t> ExchangeOrderType_;

   ExgSide GetSide() const { return static_cast<ExgSide>(this->Side_); }
};
static_assert(sizeof(ItchOrderReplace) == 36, "ItchOrderReplace 沒有 pack?");

/// 'D': 刪除委託. 完全成交時通常「不會」送出 'D', 須自行依 'E'/'C' 扣減數量.
struct ItchOrderDelete : public ItchMsgHead {
   ItchNum<uint64_t> OrderId_;
   ItchNum<uint32_t> OrderBookId_;
   char              Side_;

   ExgSide GetSide() const { return static_cast<ExgSide>(this->Side_); }
};
static_assert(sizeof(ItchOrderDelete) == 18, "ItchOrderDelete 沒有 pack?");

/// 'P': 不影響委託簿的成交(例如: 隱藏委託, 組合單的各腳).
struct ItchTrade : public ItchMsgHead {
   ItchMatchId       MatchId_;
   /// 'B', 'S', 或空白.
   char              Side_;
   ItchNum<uint64_t> Quantity_;
   ItchNum<uint32_t> OrderBookId_;
   ItchPri           TradePrice_;
   ItchAlpha<7>      OwnerParticipantId_;
   ItchAlpha<7>      CounterpartyParticipantId_;
   char              Printable_;
   char              OccurredAtCross_;
};
static_assert(sizeof(ItchTrade) == 50, "ItchTrade 沒有 pack?");

/// 'Z': 集合競價的試算撮合價.
struct ItchEquilibriumPriceUpdate : public ItchMsgHead {
   ItchNum<uint32_t> OrderBookId_;
   ItchNum<uint64_t> BidQuantity_;
   ItchNum<uint64_t> AskQuantity_;
   ItchPri           EquilibriumPrice_;
   ItchPri           BestBidPrice_;
   ItchPri           BestAskPrice_;
   ItchNum<uint64_t> BestBidQuantity_;
   ItchNum<uint64_t> BestAskQuantity_;
};
static_assert(sizeof(ItchEquilibriumPriceUpdate) == 53, "ItchEquilibriumPriceUpdate 沒有 pack?");

/// 'G': GLIMPSE 快照結束; 只出現在 GLIMPSE(SoupBinTCP), 不會出現在 MoldUDP64.
struct ItchEndOfSnapshot {
   char           MsgType_;
   /// 快照之後, 即時行情(MoldUDP64)要從這個序號開始處理; ASCII 數字.
   ItchAlpha<20>  SeqNo_;
};
static_assert(sizeof(ItchEndOfSnapshot) == 21, "ItchEndOfSnapshot 沒有 pack?");

fon9_PACK_POP;

//--------------------------------------------------------------------------//

struct ItchStateNameMap {
   const char*       StateName_;
   SetTradingPhase   Phase_;
};
/// Order Book State 的 StateName => 盤別.
/// - 底下名稱以 Genium INET 公開規格(ASX) 為主, 並加上推測的 SET 名稱.
/// - ⚠ SET 實際使用的 StateName 須以 SET ITCH 規格為準, 取得後請更新此表.
/// - 查表時會先去除尾端空白, 大小寫須相符.
static const ItchStateNameMap kItchStateNameMap[] = {
   // ----- Genium INET (ASX) -----
   {"PRE_OPEN",            SetTradingPhase::PreOpen},
   {"OPEN",                SetTradingPhase::Open},
   {"PRE_CSPA",            SetTradingPhase::PreClose},
   {"CSPA",                SetTradingPhase::PreClose},
   {"LATE_TRADING",        SetTradingPhase::OffHour},
   {"POST_CLOSE",          SetTradingPhase::Closed},
   {"CLOSE",               SetTradingPhase::Closed},
   {"ADJUST",              SetTradingPhase::PreTrading},
   {"ENQUIRE",             SetTradingPhase::PreTrading},
   {"TRADING_HALT",        SetTradingPhase::Halt},
   {"REG_HALT",            SetTradingPhase::Halt},
   {"SUSPEND",             SetTradingPhase::Suspend},
   // ----- SET (推測, 待確認) -----
   {"PRE-OPEN",            SetTradingPhase::PreOpen},
   {"PRE-OPEN1",           SetTradingPhase::PreOpen},
   {"PRE-OPEN2",           SetTradingPhase::PreOpen},
   {"OPEN1",               SetTradingPhase::Open},
   {"OPEN2",               SetTradingPhase::Open},
   {"INTERMISSION",        SetTradingPhase::Intermission},
   {"PRE-CLOSE",           SetTradingPhase::PreClose},
   {"OFF-HOUR",            SetTradingPhase::OffHour},
   {"CLOSED",              SetTradingPhase::Closed},
   {"PAUSE",               SetTradingPhase::Pause},
   {"HALT",                SetTradingPhase::Halt},
};

} // namespaces
#endif//__f9set_ExgMdItchFmt_hpp__

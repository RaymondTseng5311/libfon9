// \file f9setouch/OuchFmt.hpp
//
// SET OUCH 下單訊息格式「欄位定義表」.
//
// SET 自 2023-05-08 起使用 Nasdaq Genium INET 架構, 下單為 OUCH over SoupBinTCP.
// SET 版 OUCH 規格尚未取得(只提供給會員券商), 目前的欄位定義依據同架構的公開規格:
// - ASX Trade OUCH Message Specification (asx-trade-ouch-message-specification.pdf), Section 2.
//   ASX 的 OUCH 為 Genium INET OUCH, 與 f9set 行情模組依據的 ASX Trade ITCH 為同一套系統.
// - ASX 專屬欄位(Crossing Key, Regulatory Data, Centre Point 委託種類...) SET 應該會不同.
//
// 取得 SET OUCH 規格後, 原則上「只需要修改本檔」:
// - 訊息代碼 OuchMsgType.
// - 各訊息的欄位與長度 (每個 struct 都有 static_assert 檢查長度).
// - 列舉值: OuchSide, OuchTimeInForce, OuchOrderType, OuchCancelReason...
// - 價格小數位數 kOuchPriceDecimals.
// 下單線路(ExgOuchTradingLine) 只透過本檔的欄位名稱存取.
//
#ifndef __f9setouch_OuchFmt_hpp__
#define __f9setouch_OuchFmt_hpp__
#include "f9setouch/Config.h"
#include "fon9/fmkt/FmktTypes.hpp"
#include "fon9/CharAry.hpp"
#include "fon9/Endian.hpp"
#include "fon9/TimeStamp.hpp"

namespace f9setouch {
fon9_PACK(1);

/// OUCH 的數值欄位: unsigned big-endian binary.
template <typename T>
struct OuchNum {
   fon9::byte  Bytes_[sizeof(T)];
   T Get() const {
      return fon9::GetBigEndian<T>(this->Bytes_);
   }
   void Put(T v) {
      fon9::PutBigEndian(this->Bytes_, v);
   }
};
/// OUCH 的 Signed Numeric 欄位: 4 bytes signed big-endian.
struct OuchInt32 {
   OuchNum<uint32_t> Raw_;
   int32_t Get() const {
      return static_cast<int32_t>(this->Raw_.Get());
   }
   void Put(int32_t v) {
      this->Raw_.Put(static_cast<uint32_t>(v));
   }
};
/// OUCH 的文字欄位: 左靠右補空白; ToStrView() 時移除尾端空白.
template <size_t N>
using OuchAlpha = fon9::CharAryP<N, 0, char, ' '>;

/// OUCH 的價格: 4 bytes signed big-endian, 固定 kOuchPriceDecimals 位小數.
/// ASX: "Price: always two decimal places".
/// SET 價格檔位最小 0.01, 推測同樣為 2 位小數, 待 SET 規格確認.
static constexpr fon9::DecScaleT kOuchPriceDecimals = 2;
using OuchPrice = fon9::Decimal<int32_t, kOuchPriceDecimals>;
struct OuchPri {
   OuchInt32 Raw_;
   OuchPrice Get() const {
      return OuchPrice::Make<kOuchPriceDecimals>(this->Raw_.Get());
   }
   void Put(OuchPrice v) {
      this->Raw_.Put(v.GetOrigValue());
   }
};

/// Order Token: 由券商端產生, 每個 OUCH 帳號當日不可重複.
static constexpr size_t kOuchTokenSize = 14;
using OuchToken = OuchAlpha<kOuchTokenSize>;

/// Genium INET 的 Timestamp: 8 bytes, 1970-01-01 UTC 起的奈秒數.
struct OuchTimestamp {
   OuchNum<uint64_t> Ns_;
   /// fon9::TimeStamp 精確度為 us, 這裡直接捨去 ns 部分(不四捨五入).
   fon9::TimeStamp ToTimeStamp() const {
      return fon9::TimeStamp{fon9::TimeInterval::Make<6>(this->Ns_.Get() / 1000)};
   }
};
/// Genium INET 的 Match ID: 12 bytes = Match Number(8) + Combo Group ID(4).
struct OuchMatchId {
   OuchNum<uint64_t> MatchNo_;
   OuchNum<uint32_t> ComboGroupId_;
};
static_assert(sizeof(OuchMatchId) == 12, "OuchMatchId 沒有 pack?");

//--------------------------------------------------------------------------//

enum class OuchMsgType : char {
   // ----- Inbound: 券商 => 交易所.
   EnterOrder = 'O',
   ReplaceOrder = 'U',
   CancelOrder = 'X',
   CancelByOrderId = 'Y',
   // ----- Outbound: 交易所 => 券商.
   OrderAccepted = 'A',
   OrderRejected = 'J',
   OrderReplaced = 'U',
   OrderCancelled = 'C',
   OrderExecuted = 'E',
};

/// B=Buy, S=Sell, T=Short sell, C=Buy in a combination with short sell legs.
/// SET 規則: 放空需在可放空名單內, 價格高於最近成交價(uptick rule).
enum class OuchSide : char {
   Buy = 'B',
   Sell = 'S',
   ShortSell = 'T',
   ComboShortSell = 'C',
};

/// ASX: 0=Day, 3=Fill and Kill, 4=Fill or Kill.
/// SET 另有 GTC, GTD(隔夜單), 待 SET 規格確認代碼.
enum class OuchTimeInForce : uint8_t {
   Day = 0,
   FillAndKill = 3,
   FillOrKill = 4,
};

/// ASX 的 OUCH Order Type: Y=Limit; 其餘(N, D, S, P, B, F, T, C, E)為 ASX Centre Point 等專屬種類.
/// SET 有 Limit, Market, Market-to-Limit, ATO/ATC, 待 SET 規格確認代碼.
enum class OuchOrderType : char {
   Limit = 'Y',
};

/// 1=On book, 2=Not on book.
enum class OuchOrderState : uint8_t {
   OnBook = 1,
   NotOnBook = 2,
};

/// Order Cancelled 的原因.
enum class OuchCancelReason : uint8_t {
   ByUser = 1,
   ConnectionLoss = 4,
   FakDeletedInAuction = 9,
   ByExchangeOnBehalf = 10,
   BySystemSessionChange = 20,
   InactivatedSessionChange = 21,
   InactivatedDayOrder = 24,
};

/// ASX Regulatory Data: 44 bytes. SET 應該會不同, 暫時保留整塊, 由使用者自行填入.
struct OuchRegulatoryData {
   OuchAlpha<1>   CapacityOfParticipant_;
   OuchAlpha<1>   DirectedWholesale_;
   OuchAlpha<4>   ExecutionVenue_;
   OuchAlpha<10>  IntermediaryId_;
   OuchAlpha<20>  OrderOrigin_;
   OuchAlpha<8>   Filler_;
};
static_assert(sizeof(OuchRegulatoryData) == 44, "OuchRegulatoryData 沒有 pack?");

//--------------------------------------------------------------------------//
// Inbound: 券商 => 交易所.

struct OuchEnterOrder {
   char                 MsgType_;   // 'O'
   OuchToken            OrderToken_;
   OuchNum<uint32_t>    OrderBookId_;
   char                 Side_;
   OuchNum<uint64_t>    Quantity_;
   OuchPri              Price_;
   OuchNum<uint8_t>     TimeInForce_;
   OuchNum<uint8_t>     OpenClose_;
   OuchAlpha<10>        ClientAccount_;
   OuchAlpha<15>        CustomerInfo_;
   OuchAlpha<32>        ExchangeInfo_;
   OuchAlpha<1>         ClearingParticipant_;
   OuchInt32            CrossingKey_;
   OuchRegulatoryData   RegulatoryData_;
   char                 OrderType_;
   OuchNum<uint64_t>    ShortSellQuantity_;
   OuchNum<uint64_t>    MinimumAcceptableQuantity_;
};
static_assert(sizeof(OuchEnterOrder) == 157, "OuchEnterOrder 沒有 pack?");

/// 改單: 填入「期望的剩餘總量」及價格; Quantity=0 或 Price=0 表示不變.
/// 改單成功後, 之後的改單、刪單須使用 ReplacementOrderToken.
struct OuchReplaceOrder {
   char                 MsgType_;   // 'U'
   OuchToken            ExistingOrderToken_;
   OuchToken            ReplacementOrderToken_;
   OuchNum<uint64_t>    Quantity_;
   OuchPri              Price_;
   OuchNum<uint8_t>     OpenClose_;
   OuchAlpha<10>        ClientAccount_;
   OuchAlpha<15>        CustomerInfo_;
   OuchAlpha<32>        ExchangeInfo_;
   OuchRegulatoryData   RegulatoryData_;
   OuchNum<uint64_t>    ShortSellQuantity_;
   OuchNum<uint64_t>    MinimumAcceptableQuantity_;
};
static_assert(sizeof(OuchReplaceOrder) == 159, "OuchReplaceOrder 沒有 pack?");

/// 刪單: OrderToken 為「新單」的 Token(不是改單的 ReplacementOrderToken).
struct OuchCancelOrder {
   char        MsgType_;   // 'X'
   OuchToken   OrderToken_;
};
static_assert(sizeof(OuchCancelOrder) == 15, "OuchCancelOrder 沒有 pack?");

struct OuchCancelByOrderId {
   char              MsgType_;   // 'Y'
   OuchNum<uint32_t> OrderBookId_;
   char              Side_;      // 只能為 'B' 或 'S'.
   OuchNum<uint64_t> OrderId_;
};
static_assert(sizeof(OuchCancelByOrderId) == 14, "OuchCancelByOrderId 沒有 pack?");

//--------------------------------------------------------------------------//
// Outbound: 交易所 => 券商. 透過 SoupBinTCP Sequenced Data 送出.

struct OuchOrderAccepted {
   char                 MsgType_;   // 'A'
   OuchTimestamp        Timestamp_;
   OuchToken            OrderToken_;
   OuchNum<uint32_t>    OrderBookId_;
   char                 Side_;
   OuchNum<uint64_t>    OrderId_;
   /// 在委託簿上的剩餘量.
   OuchNum<uint64_t>    Quantity_;
   OuchPri              Price_;
   OuchNum<uint8_t>     TimeInForce_;
   OuchNum<uint8_t>     OpenClose_;
   OuchAlpha<10>        ClientAccount_;
   OuchNum<uint8_t>     OrderState_;
   OuchAlpha<15>        CustomerInfo_;
   OuchAlpha<32>        ExchangeInfo_;
   OuchAlpha<1>         ClearingParticipant_;
   OuchInt32            CrossingKey_;
   OuchRegulatoryData   RegulatoryData_;
   char                 OrderType_;
   OuchNum<uint64_t>    ShortSellQuantity_;
   OuchNum<uint64_t>    MinimumAcceptableQuantity_;
};
static_assert(sizeof(OuchOrderAccepted) == 174, "OuchOrderAccepted 沒有 pack?");

/// RejectCode: 錯誤代碼在 Genium System Error Message Reference(不在 OUCH 規格內).
struct OuchOrderRejected {
   char           MsgType_;   // 'J'
   OuchTimestamp  Timestamp_;
   OuchToken      OrderToken_;
   OuchInt32      RejectCode_;
};
static_assert(sizeof(OuchOrderRejected) == 27, "OuchOrderRejected 沒有 pack?");

struct OuchOrderReplaced {
   char                 MsgType_;   // 'U'
   OuchTimestamp        Timestamp_;
   /// 若由其他協定改單, 則為空白.
   OuchToken            ReplacementOrderToken_;
   OuchToken            PreviousOrderToken_;
   OuchNum<uint32_t>    OrderBookId_;
   char                 Side_;
   OuchNum<uint64_t>    OrderId_;
   /// 在委託簿上的剩餘量.
   OuchNum<uint64_t>    Quantity_;
   OuchPri              Price_;
   OuchNum<uint8_t>     TimeInForce_;
   OuchNum<uint8_t>     OpenClose_;
   OuchAlpha<10>        ClientAccount_;
   OuchNum<uint8_t>     OrderState_;
   OuchAlpha<15>        CustomerInfo_;
   OuchAlpha<32>        ExchangeInfo_;
   OuchAlpha<1>         ClearingParticipant_;
   OuchInt32            CrossingKey_;
   OuchRegulatoryData   RegulatoryData_;
   char                 OrderType_;
   OuchNum<uint64_t>    ShortSellQuantity_;
   OuchNum<uint64_t>    MinimumAcceptableQuantity_;
};
static_assert(sizeof(OuchOrderReplaced) == 188, "OuchOrderReplaced 沒有 pack?");

struct OuchOrderCancelled {
   char              MsgType_;   // 'C'
   OuchTimestamp     Timestamp_;
   OuchToken         OrderToken_;
   OuchNum<uint32_t> OrderBookId_;
   char              Side_;
   OuchNum<uint64_t> OrderId_;
   /// OuchCancelReason.
   OuchNum<uint8_t>  Reason_;
};
static_assert(sizeof(OuchOrderCancelled) == 37, "OuchOrderCancelled 沒有 pack?");

struct OuchOrderExecuted {
   char              MsgType_;   // 'E'
   OuchTimestamp     Timestamp_;
   OuchToken         OrderToken_;
   OuchNum<uint32_t> OrderBookId_;
   OuchNum<uint64_t> TradedQuantity_;
   OuchPri           TradePrice_;
   OuchMatchId       MatchId_;
   /// 1=Continuous, 20=Auction...
   OuchNum<uint16_t> DealSource_;
   /// bit 0: 0=passive, 1=aggressive.
   OuchNum<uint8_t>  MatchAttributes_;
};
static_assert(sizeof(OuchOrderExecuted) == 54, "OuchOrderExecuted 沒有 pack?");

fon9_PACK_POP;
} // namespaces
#endif//__f9setouch_OuchFmt_hpp__

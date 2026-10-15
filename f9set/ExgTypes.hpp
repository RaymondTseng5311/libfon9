// \file f9set/ExgTypes.hpp
#ifndef __f9set_ExgTypes_hpp__
#define __f9set_ExgTypes_hpp__
#include "f9set/Config.h"
#include "fon9/StrView.hpp"
#include <stdint.h>

namespace f9set {

/// SET 的商品識別: ITCH 的 Order Book ID.
using OrderBookId = uint32_t;
/// ITCH Order ID: 只在 [OrderBookId + Side] 之內唯一.
using ItchOrderId = uint64_t;
/// ITCH 的價格: 有號整數, 小數位數由 Order Book Directory 決定.
using ItchPrice = int32_t;
using ItchQty = uint64_t;

enum class ExgSide : char {
   Buy = 'B',
   Sell = 'S',
};

/// 盤別.
/// - SET 的開/收盤時間 T1,T2,T3 為隨機結束, 動態價格區間也會讓個股進入 Pre-open.
/// - 所以盤別「只能」依行情的 Order Book State 訊息判斷, 不可依時間推算.
/// - State Name => SetTradingPhase 的對照表, 在 f9set/ExgMdItchFmt.hpp 的 kItchStateNameMap.
enum class SetTradingPhase : uint8_t {
   /// 尚未收到 Order Book State, 或收到無法辨識的 State Name.
   Unknown = 0,
   /// 開盤前 (尚未進入 Pre-open).
   PreTrading,
   /// 集合競價(Pre-open I/II, 動態價格區間觸發的 2 分鐘 Pre-open).
   PreOpen,
   /// 連續撮合(Session I/II).
   Open,
   /// 午休.
   Intermission,
   /// 收盤集合競價(Pre-close).
   PreClose,
   /// 盤後 Trade Report (Off-hour).
   OffHour,
   /// 收盤.
   Closed,
   /// 異常交易暫停 (Pause).
   Pause,
   /// 暫停交易 (Halt).
   Halt,
   /// 停牌 (Suspension).
   Suspend,
};
f9set_API fon9::StrView ToStrView(SetTradingPhase v);
/// 依 f9set/ExgMdItchFmt.hpp 的 kItchStateNameMap 轉換; 無法辨識則傳回 SetTradingPhase::Unknown.
f9set_API SetTradingPhase ItchStateNameToPhase(fon9::StrView stateName);

/// 是否為集合競價時段.
static inline bool IsAuctionPhase(SetTradingPhase v) {
   return v == SetTradingPhase::PreOpen || v == SetTradingPhase::PreClose;
}

} // namespaces
#endif//__f9set_ExgTypes_hpp__

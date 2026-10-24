// \file f9setouch/OuchFmtStr.hpp
//
// 將 OUCH 訊息轉成文字, 用於 log 及除錯.
//
#ifndef __f9setouch_OuchFmtStr_hpp__
#define __f9setouch_OuchFmtStr_hpp__
#include "f9setouch/OuchFmt.hpp"
#include "fon9/buffer/RevBuffer.hpp"

namespace f9setouch {

/// 券商送出的訊息(Enter, Replace, Cancel...), 例: "O|token=...|obid=...".
/// 長度不正確或無法辨識, 則輸出 "?|type=..|sz=..".
f9setouch_API void RevPrintOuchInbound(fon9::RevBuffer& rbuf, const void* msg, unsigned msgsz);
/// 交易所送出的回報(Accepted, Rejected, Replaced, Cancelled, Executed).
f9setouch_API void RevPrintOuchOutbound(fon9::RevBuffer& rbuf, const void* msg, unsigned msgsz);

} // namespaces
#endif//__f9setouch_OuchFmtStr_hpp__

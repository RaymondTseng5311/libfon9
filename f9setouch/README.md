# f9setouch: SET (泰國證交所) OUCH 下單 (QTS-Tailand PoC)

SET 自 2023-05-08 起使用 Nasdaq Genium INET 交易系統, 下單協定為 OUCH over SoupBinTCP (另可用 FIX).
SET 自己的 OUCH 規格不公開(需透過會員券商申請), 所以目前的訊息格式依照同為 Genium INET 的
**ASX Trade OUCH** 實作, 與 `f9set` (ITCH 行情, 依照 ASX Trade ITCH v3.4) 一致.

## 拿到 SET 規格後要改的地方

所有 SET 特有的欄位、代碼、長度都集中在 `OuchFmt.hpp`, 其他檔案原則上不用改:

| 待確認 | 位置 | 目前假設 (ASX) |
|---|---|---|
| 價格小數位數 | `kOuchPriceDecimals` | 2 位, int32 |
| TimeInForce | `OuchTimeInForce` | 0=Day, 3=IOC, 4=FOK (GTC/GTD 未定義) |
| OrderType | `OuchOrderType` | 只有 'Y'=Limit (Market/MTL/ATO/ATC 未定義) |
| Regulatory Data 區塊 | `OuchRegulatoryData` | 44 bytes, 填空白 |
| 各訊息長度 | `static_assert(sizeof(...))` | Enter 157, Replace 159, Cancel 15, Accepted 174, Rejected 27, Replaced 188, Cancelled 37, Executed 54 |
| Cancel reason / Reject code | `OuchCancelReason` / `RejectCode_` | ASX 代碼 |

改完 `OuchFmt.hpp` 後, 同步調整 `OuchFmtStr.cpp` (log 文字) 與 `ExgOuchTrade.cpp` (手動指令參數).

## 檔案

- `OuchSoupBinTcp.*`: SoupBinTCP 封包 (Login/Heartbeat/Logout/Unsequenced) 與用戶端解析 `OuchSoupClient`.
- `OuchFmt.hpp`: OUCH 訊息格式 (可替換).
- `OuchFmtStr.*`: 訊息轉成文字, 用於 log.
- `ExgOuchTypes.*`: 下單要求 `ExgOuchRequest`、Order Token 產生器、回報介面 `ExgOuchRptHandler`、線路參數.
- `ExgOuchLineMgr.*`: 線路管理員 (`f9fmkt::TradingLineManager` + `fon9::IoManagerTree`).
- `ExgOuchTradingLine.*`: 一條 OUCH 線路 (登入、心跳、逾時、流量管制、回報分派、log).
- `ExgOuchLineFactory.*`: SessionFactory "SetOuch".
- `ExgOuchTrade.*`: plugin "SetOuchTrade" 與手動下單指令.
- `f9setOuch_UT.cpp`: 單元測試 + 端對端測試 (本機假 OUCH 伺服器).

## 設定範例

以 fon9 管理指令設定 (格式同 `f9tws/README.md`; 需先載入 `TcpClient` 裝置 plugin):
```
# 主程式已連結 f9setouch 時用 EntryName=SetOuchTrade;
# 動態載入則用 FileName=libf9setouch.so,EntryName=f9p_SetOuchTrade
ss,Enabled=Y,EntryName=SetOuchTrade,Args='Name=SetOuch|TokenPrefix=A|Io={SvcCfg="ThreadCount=1"}' /MaPlugins/SetOuchTrade

ss,Enabled=Y,Session=SetOuch,SessionArgs='User=user01|Pass=secret|Hb=1|RxTimeout=15|Fc=10/1000',Device=TcpClient,DeviceArgs='10.x.x.x:port|ClosedReopen=5' /SetOuch/^edit:Config/Ouch1
gv /SetOuch/^apply:Config
/SetOuch/^apply:Config submit 1
```
- `TokenPrefix`: 最多 4 碼; 同一個 OUCH 帳號若有多個系統同時下單, 必須用不同前置.
- `Io.Cfg` 預設 `{Name}_io.f9gv`, 也可以直接編輯該檔 (單元測試即用此格式):
  `Ouch1, SetOuch, User=user01|Pass=secret|Hb=1|Fc=10/1000, TcpClient, 10.x.x.x:port|ClosedReopen=5`

SessionArgs:

| 參數 | 說明 | 預設 |
|---|---|---|
| User / Pass | SoupBinTCP 帳密, 1..6 / 1..10 碼 | 必填 |
| Session | 指定 session, 空白=由交易所決定(目前 session) | 空白 |
| SeqNo | 首次登入要求的回報序號, 1=從頭重送 | 1 |
| Hb | 多久沒送資料就送 Heartbeat | 1 秒 |
| RxTimeout | 多久沒收到資料就斷線(含登入逾時), 須 > Hb | 15 秒 |
| Fc | 流量管制 `筆數/毫秒` | 不管制 |
| LineFc | 線路層的流量管制 | 不管制 |
| Log | 是否寫 log `SetOuch_{User}.log` | Y |

交易所規定連線後 1 秒內必須送出登入, 本模組在 LinkReady 時立即送出.

## Session 與回報序號

- 登入成功後記下交易所回覆的 session; 斷線重連時用 **同一個 session + 下一個期望序號** 登入, 不會收到重複回報.
- 程式重新啟動時, 序號從 `SeqNo` 參數開始 (預設 1, 交易所會重送當日全部回報).
  序號目前沒有存檔, OMS 必須用 seqNo 排除重複回報.
- 回報經由 `ExgOuchRptHandler` 送出, 參數包含 seqNo; 預設的 `ExgOuchRptLogger` 只寫 log.

## Order Token

- 14 碼 = TokenPrefix + 數字補零; 數字起始值 = 啟動時的當地日內毫秒 × 1000, 所以同一天重啟不會重複.
- 改單 (Replace) 會產生新 token, `ExistingToken` 填原 token.
- 刪單使用該筆委託的 **原始(新單)token**.

## 手動下單指令 (PoC)

在 `/SetOuch` 節點執行 seed command:
```
new OrderBookId Side Qty Price [Tif=0|3|4] [Acc=] [Token=] [SsQty=]
    例: new 1001 B 100 34.25 Acc=ACC001
    Side: B=買, S=賣, T=放空(沒給 SsQty 則 SsQty=Qty)
chg ExistingToken Qty Price [Token=]    Qty=0 表示只改價
del Token
```
結果例: `token=A0046890285000|result=Sent`, result 可能為 Sent / NoReadyLine / RejectRequest / Queuing.

## 測試

```
cmake -DCMAKE_FON9_BUILD_UNIT_TEST=1 ... && make f9setOuch_UT
./f9setouch/f9setOuch_UT [--keep]
```
端對端測試涵蓋: 登入、新/改/刪單、成交、拒單、流量管制、心跳、斷線續傳(無重複回報)、RxTimeout、登入被拒重試、log 內容.

## 尚未完成

- SET 專屬的 TIF (GTC/GTD)、Order Type (Market/MTL/ATO/ATC)、Regulatory Data 內容.
- Cancel by Order ID ('Y') 訊息已定義格式, 但手動指令尚未提供.
- 與 OMS 整合 (目前只有回報介面 + log).
- 回報序號存檔.
- 使用者 Mac 上的 `Ouch5.0.pdf` 是 Nasdaq US 的 OUCH 5.0, 與 Genium INET 格式不同, 不能用來核對;
  需要 ASX Trade OUCH 規格或 SET 正式規格.

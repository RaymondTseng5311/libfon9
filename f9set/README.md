fon9 泰國證券交易所(SET) 行情模組
================================

## 基本說明
* SET 自 2023-05-08 起使用 Nasdaq Genium INET 架構: 行情為 ITCH, 透過 MoldUDP64(UDP multicast) 傳送.
* SET 版 ITCH 規格尚未取得, 目前依據同架構的公開規格實作:
  * ASX Trade ITCH Message Specification v3.4: 訊息欄位, MoldUDP64.
  * Nasdaq Genium INET ITCH Multicast (Nordics) v2.1: A/B 雙路 multicast 及回補(re-request)架構.
* 取得 SET ITCH 規格後, 原則上只需要修改 `f9set/ExgMdItchFmt.hpp`:
  * 訊息代碼、欄位、長度(每個訊息都有 `static_assert` 檢查長度).
  * 盤別名稱對照表 `kItchStateNameMap`.

## 元件
| 檔案 | 說明 |
|---|---|
| `ExgMdItchFmt.hpp` | ITCH 訊息欄位定義(可替換的表頭), big-endian 數值欄位 `ItchNum<>`, 價格 `ItchPri` |
| `ExgMdMoldUdp64.hpp` | `MoldUdp64Receiver`: 解析 Downstream Packet, 序號檢查, 跳過重複(A/B 雙路可同時餵入), 缺號通知, Heartbeat, End of Session, 換 Session |
| `ExgMdItchParser.hpp` | `ExgMdItchParser`: 依 MsgType 分派到 `OnItch*()`, 處理 'T'(Seconds) 時間 |
| `ExgMdOrderBook.hpp` | `ExgMdOrderBook`: 逐筆委託(Market by Order)建立價位, 取五檔(`fon9::fmkt::SymbBSData`)或任意檔數(SET 為十檔) |
| `ExgMdBookBuilder.hpp` | `ExgMdBookBuilder`: 商品基本資料, 委託簿, 成交, 盤別, 集合競價試算; 透過 `OnBookChanged()`, `OnDeal()`, `OnPhaseChanged()` 通知 |

## 盤別
* SET 的開/收盤時間(T1, T2, T3)為隨機結束, 動態價格區間觸發時個股會進入 2 分鐘 Pre-open.
* 因此盤別**只依** ITCH Order Book State('O') 訊息判斷, 不依時間推算.
* 無法辨識的 StateName 會成為 `SetTradingPhase::Unknown`, 原始名稱保留在 `ExgMdOrderBook::StateName_`.

## 成交計算
* 'E': 連續撮合, 只有委託簿上被成交的那一方會送出, 全部計入.
* 'C', 'P': 只計入 `Printable == 'Y'`, 避免集合競價時買賣雙方重複計算.

## 尚未處理
* MoldUDP64 缺號回補(re-request), GLIMPSE 快照(盤中啟動).
* 組合單('M'), 檔位表('L')的保存.
* 與 fon9 `fmkt::MdSymbs` / MdRts 的整合, 及 Io Session(Dgram multicast)的設定.

## 單元測試 f9setExgMdItch_UT
* 不帶參數: 用手工封包測試全部功能.
* `--save FileName`: 將測試用的封包存成錄製檔.
* `f9setExgMdItch_UT FileName`: 重播錄製檔並列出統計.
  * 錄製檔格式: 每個 UDP payload 前面加上 2 bytes(big-endian) 的長度.

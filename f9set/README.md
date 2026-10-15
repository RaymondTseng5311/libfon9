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
| `ExgMdBookBuilder.hpp` | `ExgMdBookBuilder`: 商品基本資料, 委託簿, 成交, 盤別, 集合競價試算; 透過 `OnBookChanged()`, `OnDeal()`, `OnPhaseChanged()` 通知; GLIMPSE 快照 `BeginSnapshot()` ... 'G' |
| `ExgMdSoupBinTcp.hpp` | `SoupBinTcpClient`: SoupBinTCP 封包解析(Login Accepted/Rejected, Sequenced Data, Heartbeat...)及 Login/Heartbeat/Logout 封包 |
| `ExgMdSymbs.hpp` | `ExgMdSymb`, `ExgMdSymbs`: fon9 商品資料表(Base, Ref, Deal, BS, Open/High/Low, Rt) |
| `ExgMdRtsBuilder.hpp` | `ExgMdRtsBuilder`: 將委託簿、成交、盤別寫入 `ExgMdSymbs` 並透過 MdRts 發行 |
| `ExgMdSystem.hpp` | `ExgMdSystem`: 行情系統(`fon9::fmkt::MdSystemWithHb`), 整合 A/B multicast、回補、GLIMPSE, 缺號回補計時器 |
| `ExgMdReceiverSession.hpp` | `ExgMdMoldSession`(Mc, Rr), `ExgMdGlimpseSession`(GLIMPSE) |
| `ExgMdReceiverFactory.hpp` | Session Factory "SetMdRecv", SessionArgs 說明 |
| `ExgMdSystemPlugin.cpp` | Plugin "SetMdSys", 參數說明 |

## 系統配置
```
/SetMd                ExgMdSystem (plugin: SetMdSys)
  ├─ Symbs            商品資料表, SymbId = ITCH Symbol(例: PTT, PTT-F, PTT-R)
  └─ SetMd_io         IoManager, Session Factory = SetMdRecv
       ├─ McA         Type=Mc       Dgram      Group=233.x.x.x|Bind=port|Interface=本機IP
       ├─ McB         Type=Mc       Dgram      (B 路, 與 A 路由序號過濾重複)
       ├─ Rr          Type=Rr       Dgram      回補伺服器IP:port
       └─ Glimpse     Type=Glimpse|User=|Pass=  TcpClient  GLIMPSE伺服器IP:port
```
* Plugin 範例: `Name=SetMd|TimeZone=7|NoDataEventSch='Start=06:00|End=18:00'|Io={SvcCfg='ThreadCount=1'}`
  * 其他參數(AddMarketSeq, UseRtiForRecover, SnapshotOnLost, GapWait, RetryInterval, MaxRetry) 請參考 `ExgMdSystemPlugin.cpp`.
* `PkLog=FileName` (Mc, Rr): 錄製收到的封包, 檔名為 `logs/yyyymmdd/SetMd_FileName`, 可用 `f9setExgMdItch_UT` 重播.

## 缺號回補與快照
* 全部來源(A/B multicast, 回補, GLIMPSE)都在同一個 `ExgMdSystem` 裡面, 由 MoldUDP64 序號整合, 處理時加鎖.
* 發現缺號: 缺號之後的訊息先暫存, 等 `GapWait`(預設 20ms, 另一路可能補上), 然後透過 Rr 送出 Request Packet.
  * 每次最多要求 `MaxRequestCount`(1000) 筆; 回覆的封包同樣依序號處理.
  * 每 `RetryInterval`(500ms) 沒有進展就重送, 超過 `MaxRetry`(3) 次, 或沒有 Rr 可用時, 放棄缺號.
  * 放棄缺號後委託簿可能不正確, 若有 Glimpse(且 `SnapshotOnLost=Y`), 則重新連線取得快照.
* GLIMPSE(盤中啟動):
  * 連線成功即開始等待快照: 委託簿清除, 期間的即時訊息全部暫存;
  * Login 時序號為 1, 依序處理快照訊息, 收到 'G'(End of Snapshot) 後, 丟棄快照已包含的即時訊息, 再依序處理暫存的訊息;
  * 完成後送出 Logout 並斷線; 快照完成前斷線則放棄快照, 從暫存的第一個訊息開始處理.
  * 每秒送出 Client Heartbeat, 15 秒沒收到任何資料則斷線.

## MdRts 發行
| 來源 | 發行 |
|---|---|
| Order Book Directory('R') | 建立商品; BaseInfoTw(TradingMarket='S', ShUnit=RoundLotSize); NameUTF8(LongName) |
| 委託簿異動 | 五檔有變才發行 SnapshotBS; 每個 MoldUDP64 封包處理完畢後一次發行; 快照期間不發行 |
| 成交('E', 'C'/'P' 的 Printable=Y) | DealPack(格式同 f9tws Fmt6), Open/High/Low |
| Order Book State('O') | Base 的 Session, SessionSt, TwsFlags(撮合方式: Pre-open/Pre-close 為集合競價) |
* 數量單位為「股」; `ShUnit` 為一個交易單位(Round Lot)的股數.
* 時間: ITCH 為 UTC, 發行的 DayTime 為交易所當地時間(`TimeZone`, 預設 UTC+7).
* MarketSeq = MoldUDP64 訊息序號.
* 盤別對照 `ToTradingSessionSt()`: 午休、Pause、Halt、Suspend 都對應 `f9fmkt_TradingSessionSt_Halted`.

## 盤別
* SET 的開/收盤時間(T1, T2, T3)為隨機結束, 動態價格區間觸發時個股會進入 2 分鐘 Pre-open.
* 因此盤別**只依** ITCH Order Book State('O') 訊息判斷, 不依時間推算.
* 無法辨識的 StateName 會成為 `SetTradingPhase::Unknown`, 原始名稱保留在 `ExgMdOrderBook::StateName_`.

## 成交計算
* 'E': 連續撮合, 只有委託簿上被成交的那一方會送出, 全部計入.
* 'C', 'P': 只計入 `Printable == 'Y'`, 避免集合競價時買賣雙方重複計算.

## 尚未處理
* 組合單('M'), 檔位表('L')的保存.
* 集合競價試算('Z' Equilibrium Price)尚未發行到 MdRts.
* 參考價、漲跌停: ITCH 公開規格沒有提供, 須待 SET 規格確認來源.
* SET 實際的 GLIMPSE 'G' 訊息格式、Re-request 伺服器的位址與限制, 須待 SET 規格確認.

## 單元測試 f9setExgMdItch_UT
* 不帶參數: 用手工封包測試全部功能(MoldUDP64、委託簿、成交、盤別、缺號回補、SoupBinTCP、GLIMPSE 快照、MdRts 發行).
* `--save FileName`: 將測試用的封包存成錄製檔.
* `f9setExgMdItch_UT FileName`: 重播錄製檔並列出統計.
  * 錄製檔格式: 每個 UDP payload 前面加上 2 bytes(big-endian) 的長度.

## 整合測試 f9setMdSys_UT
* 透過 plugin 建立 `ExgMdSystem`, 在本機啟動假的 UDP 發送端、回補伺服器、GLIMPSE 伺服器, 驗證:
  * 盤中啟動: GLIMPSE 快照 + 快照期間的即時封包;
  * 缺號: 送出回補要求, 回補封包補上缺號;
  * 回補失敗: 放棄缺號, 重新取得 GLIMPSE 快照;
  * PkLog 錄製格式.

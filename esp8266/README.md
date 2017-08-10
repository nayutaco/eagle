esp8266
====

### ESP8266 + Non-OS SDK 1.5.2
* WROOM-02
	* SPI SPEED : 40MHz
	* SPI MODE : QIO
	* FLASH SIZE : 32Mbit-C1
	* address
		* 0x000000 : boot_v1.5.bin
		* 0x001000 : user1.4096.new.6.bin
		* 0x3fc000 : esp_init_data_default.bin
		* 0x0fe000 : blank.bin
		* 0x3fe000 : blank.bin
* UART0 : 115200bps
	* mbed

* UART1 : 115200bps
	* デバッグログ


### OSS
* Bloom Filter
	[picocoin : jgarzik@github](https://github.com/jgarzik/picocoin)

* NTP
	[Simple NTP : raburton@github](https://github.com/raburton/esp8266/tree/master/ntp)


### FLASH

|sector |size |description |
|:--|--:|:--|
|0x200 | 500 | トランザクション情報 |
|0x3F4 | 1 | 最後に受信したblock hash(1) |
|0x3F5 | 1 | 最後に受信したblock hash(2) |
|0x3F6 | 1 | 退避セクタ(未使用) |
|0x3F7 | 1 | 退避セクタ番号(未使用) |
|0x3F9 | 3 | 空き |
|0x3FB | 1 | Bitcoinアドレス、公開鍵 |


### 実行
	* 電源を入れると、通電LEDが0.5秒間隔で点滅する
		* PEERと通信して、FLASHの準備をしている
		* LEDが消灯したら、準備完了
			* どのくらい時間がかかるかは、状況次第。1、2時間かかる可能性もある。
	* mbedのUserボタン(青色)を押すと、FLASHを全消去してやり直す
		* LEDが0.2秒間隔で点滅する
		* FLASH消去が終わると、自動的に再起動し、また PEERとの通信をし始める。
		* FLASHの準備ができると、最後に受信したBlockからしか始めないため、デバッグ用に用意している。

### ソース埋め込み
* TestNet3
	* user/user_main.c
		* PORT
	* user/bc_proto.c
		* BC_MAGIC_TESTNET3
		* BC_PORT_TESTNET3

* Bitcoinプロトコルバージョン
	* user/bc_proto.c
		* BC_PROTOCOL_VERSION

* UserAgent
	* user/bc_proto.c
		* BC_VER_UA

* PEER
	* user/user_main.c
		* MY_HOST1, MY_HOST2, MY_HOST3, MY_HOST4
		* MY_HOSTでDNS名前解決するには、ソース修正も必要(現在はIP固定のみ)

* WiFi Access Point
	* include/user_config.h
		* MY_SSID
		* MY_PASSWD

* 所有者公開鍵、プラグBitcoinアドレス
	* mbed側のソース埋め込み
		* main.cpp
			* kBitcoinAddr[]
			* kPubKey[]

* 1回のheadersに対して作成するgetdataのinv数
	* user/bc_proto.c
		* GETDATA_NUM
		* 送信バッファサイズ内でパケットを作っているため、制限が発生する
			* 分割送信にすることも可能と思われるが、ESP8266の送信完了コールバックを待つなど複雑になりそう


### WROOM-02のバッファ情報
	* espconn_get_packet_info()で取得
		* 送信バッファ : 2920
		* 受信バッファ : 1460


### 深く考慮できていない箇所
	* mbedとのUART送受信
		* 基本は「ヘッダ(8byte) + Len(1byte:Lenを含まないサイズ) + Cmd(1byte) + データ」
		* ESP8266-->mbedのヘッダは「NaYuTaCo」
		* mbed-->ESP8266のヘッダは「CoTaYuNa」
			* ヘッダが長い理由は、ESP8266の通信速度がデフォルトでmbedと通信できる速度になっていないこと。
			* にもかかわらず、ESP8266はアプリが処理できる前にログを出すので、止めようがないこと。
	* FLASH消去
		* 502セクタ消すのに、25秒程度かかる
	* FLASH更新(block hash)
		* 最後のBlockを受信したときに更新する
		* 次回起動時、更新したblock hashからgetheadersする
		* そのため、block hashを保存したときには通電に関するTXはFLASHに保存済みでなくてはならない
			* TXはmempoolの段階で処理するので、基本的には大丈夫なはず
			* だが、タイミングですれ違うパターンがあるかもしれない
	* version
		* Heightは0
	* getheaders
		* block locator hashesは最新のBlock Hashのみとしている
			* 
	* [RNG](http://esp8266-re.foogod.com/wiki/Random_Number_Generator)
		* 不安なので、使わない
		* 今回は乱数は重要ではないため、espconn_get_packet_info()のpackseq_nxtをsrand()に与える

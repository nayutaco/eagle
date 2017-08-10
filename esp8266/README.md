# esp8266_bitcoin

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


### ビルド

[Nayuta GoogleDrive://EAGLE/hirokuma/work/WROOM-02/開発環境](https://drive.google.com/open?id=0B33sikPWkTjQZnNMS1NtQ1ducUE)参照

* コンパイル環境(VirtualBox)
	1. [ここを参考](http://bbs.espressif.com/viewtopic.php?f=57&t=2)にVirtualBoxの仮想環境取得
		* [GoogleDrive : ESP8266_lubuntu.ova](https://drive.google.com/open?id=0B33sikPWkTjQUHJSREd5T2IyRDg)
	2. 同じサイトにコンパイラのアップデート版があるため、それも取得
		* GoogleDrive版は更新済み
	3. [Getting Started Guide](http://bbs.espressif.com/download/file.php?id=1074)を参考に環境を作る

* SDK(Getting Started Guideの説明と重複する)
	1. [ESP8266_NONOS_SDK_V1.5.2_16_01_29.zip](http://bbs.espressif.com/download/file.php?id=1079)をダウンロード
		* [GoogleDrive : ESP8266_NONOS_SDK_V1.5.2_16_01_29_eagleplug.zip](https://drive.google.com/open?id=0B33sikPWkTjQVjdkYjgwanR1MkE)
	2. ローカル環境に展開する
	3. VirtualBoxで共有フォルダとして見えるように設定する
	4. esp8266_kumacoincをesp_iot_sdk_v1.5.2の直下に展開する(esp_iot_sdk_v1.5.2/esp8266_kumacoinc/README.md、という位置)
		* git : https://bitbucket.org/nayuta_co/eagle_wifi
			* 使うのは、esp8266_kumacoincのみ

* ソース変更
	* include/user_config.h
		* user_config_sample.hをリネーム
		* 自分のWiFi APに合わせて、MY_SSIDとMY_PASSWDを変更する
	* user/user_main.c
		* MY_HOST, MY_HOST1～4を PEERのIPアドレスに変更する
			* 今のところ、固定IPのため、MY_HOSTは使っていない
		* PORTを、使用するポート番号に変更する
			* 18333はTestNet3

* ビルド
	1. VirtualBox上で、コンソールからesp_iot_sdk_v1.5.2/esp8266_kumacoincに移動
	2. 以下のコマンドを打つ
		> ./gen_misc.sh

	3. "!!!"の出力まで出れば、だいたいうまく行っている
		* 消したいときは「make clobber」

* 焼く(Windows)
	1. [FLASH Download Tool](http://bbs.espressif.com/viewtopic.php?f=57&t=433)を取得
		* [GoogleDrive : FLASH_DOWNLOAD_TOOLS_v2.4_150924.zip](https://drive.google.com/open?id=0B33sikPWkTjQMHZ6RUYwMjAwU2s)
		* [User Manual](http://bbs.espressif.com/viewtopic.php?f=51&t=1376)

	2. ローカル環境の「esp_iot_sdk_v1.5.2\bin\upgrade\user1.4096.new.6.bin」が焼くバイナリファイル
	3. [画像](https://drive.google.com/open?id=0B33sikPWkTjQU01HaExmcTV6bUk)を参考に設定する
		* タイトルが「V2.3」となっているが、間違いらしい
		* 生成したファイル以外も、sp_iot_sdk_v1.5.2\binの中に入っている
			* 0x000000 : sp_iot_sdk_v1.5.2\bin\boot_v1.5.bin
			* 0x001000 : sp_iot_sdk_v1.5.2\bin\upgrade\user1.4096.new.6.bin
			* 0x3fc000 : sp_iot_sdk_v1.5.2\bin\esp_init_data_default.bin
			* 0x0fe000 : sp_iot_sdk_v1.5.2\bin\blank.bin
			* 0x3fe000 : sp_iot_sdk_v1.5.2\bin\blank.bin
		* 5ファイルとも焼くのは初回だけで、あとは2番目の「user1.4096.new.6.bin」だけでよい
	4. [画像](https://drive.google.com/open?id=0B33sikPWkTjQNFNXZl9Qd1ZnNVE)を参考に、PCとUARTを接続する
		* DOWNLOAD TOOLSのCOMポート番号は20までしか対応してないので、その数字になるようにPC側を設定すること
	5. SW2(橙色)を押したまま、Eagle Plugの電源を入れる
	6. 左下の「START」を押す
		* うまく行くと、「Download」になってプログレスバーが進む
		* UARTの通信がうまく行かないと、失敗する
			* SW2をちゃんと押せていない場合も
		* 初めて焼くとき、「MAC Address」のメモを残しておくこと！
			* 通常は問題ないが、ここでしかMACアドレスを確認できないため
	7. 表示が「FINISH」になったら、Eagle Plugの電源を切り、UARTボードも外す

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

### OSS
* Bloom Filter
	[picocoin : jgarzik@github](https://github.com/jgarzik/picocoin)

* NTP
	[Simple NTP : raburton@github](https://github.com/raburton/esp8266/tree/master/ntp)


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

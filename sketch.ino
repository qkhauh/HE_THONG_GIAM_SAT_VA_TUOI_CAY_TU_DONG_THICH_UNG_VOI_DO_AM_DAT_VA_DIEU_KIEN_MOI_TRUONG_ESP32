#include <WiFi.h> //Kết nối ESP32 với mạng Wi-Fi.
#include <WiFiClientSecure.h> //Hỗ trợ kết nối mạng bảo mật TLS/SSL, thường dùng với HTTPS.
#include <HTTPClient.h> //Gửi yêu cầu HTTP đến máy chủ, chẳng hạn ThingSpeak.
#include <DHT.h> //Đọc cảm biến nhiệt độ và độ ẩm DHT22.
#include <Wire.h> //Giao tiếp I2C, được dùng để kết nối LCD với ESP32.
#include <LiquidCrystal_I2C.h> //Điều khiển màn hình LCD thông qua I2C.
#include <math.h> //Cung cấp các hàm toán học; code dùng isnan() để kiểm tra giá trị không hợp lệ.

// ==================================================
// 1. CAU HINH WIFI VA THINGSPEAK
// ==================================================
const char* WIFI_SSID = "Wokwi-GUEST"; //lưu địa chỉ 
const char* WIFI_PASSWORD = "";

// Thay bang Write API Key cua kenh ThingSpeak cua ban.
// Neu key da bi chia se cong khai, hay tao key moi.
const char* THINGSPEAK_WRITE_KEY = ""; 

// ==================================================
// 2. KHAI BAO CHAN ESP32
// ==================================================
#define SOIL_PIN 34 //Chân GPIO34 nhận tín hiệu analog từ biến trở mô phỏng độ ẩm đất.
#define DHT_PIN 32 //Chân GPIO32 nhận tín hiệu từ cảm biến DHT22.
#define DHTTYPE DHT22 //Xác định loại cảm biến đang sử dụng là DHT22.

#define PUMP_PIN 23 //GPIO23 xuất tín hiệu điều khiển LED mô phỏng máy bơm.
#define MODE_BUTTON_PIN 14 //GPIO14 đọc nút chuyển đổi AUTO/MANUAL.
#define PUMP_BUTTON_PIN 12 //GPIO12 đọc nút bật/tắt bơm thủ công.

#define LCD_SDA_PIN 21 //Chân SDA truyền dữ liệu I2C.
#define LCD_SCL_PIN 22 //Chân SCL cung cấp xung đồng hồ I2C.

// ==================================================
// 3. KHOI TAO THIET BI
// ==================================================
DHT dht(DHT_PIN, DHTTYPE); //chân kết nối cảm biến, loại cảm biến DHT22.

LiquidCrystal_I2C lcd(0x27, 16, 2); //

WiFiClientSecure secureClient;

// ==================================================
// 4. CAC THONG SO HE THONG
// ==================================================

// Nguong do am dat: bat bom < 40%, tat bom >40% 
const int MOISTURE_ON = 40; //
const int MOISTURE_OFF = 41; //

// Nguong danh gia thoi tiet (gia tri thu nghiem) 
const float TEMP_HOT = 32.0; //Nhiệt độ từ 32°C trở lên được xem là nóng.
const float TEMP_COOL = 28.0; //Nhiệt độ từ 28°C trở xuống được xem là mát/lạnh theo quy tắc mô phỏng.
const float AIR_HUMIDITY_DRY = 40.0; //Độ ẩm không khí từ 40% trở xuống được xem là thấp.
const float AIR_HUMIDITY_HIGH = 65.0; //Độ ẩm không khí từ 65% trở lên được xem là cao.

// Gia tri ADC cua bien tro r
const int DRY_RAW = 0; //giả định giá trị 0 tương ứng với đất khô.
const int WET_RAW = 4095; //giả định giá trị 4095 tương ứng với đất rất ẩm.

// Thoi gian an toan cho bom
const unsigned long MAX_PUMP_RUN_MS = 30000; //30 giay
const unsigned long PUMP_COOLDOWN_MS = 5000; //5 giay

// Chu ky doc va cap nhat r
const unsigned long SOIL_INTERVAL_NORMAL_MS = 500; //Đọc đất mỗi 0,5 giây khi ưu tiên bình thường.
const unsigned long SOIL_INTERVAL_HIGH_MS = 200; //Đọc đất mỗi 0,2 giây khi ưu tiên cao.
const unsigned long SOIL_INTERVAL_LOW_MS = 1000; //Đọc đất mỗi 1 giây khi ưu tiên thấp.
const unsigned long DHT_INTERVAL_MS = 2000; //Đọc DHT22 mỗi 2 giây.
const unsigned long LCD_INTERVAL_MS = 500; //Cập nhật LCD mỗi 0,5 giây.
const unsigned long SERIAL_INTERVAL_MS = 5000; //In trạng thái ra Serial mỗi 2 giây.
const unsigned long THINGSPEAK_INTERVAL_MS = 16000; //Chu kỳ gửi dữ liệu dự kiến là 16 giây.
const unsigned long DEBOUNCE_MS = 50; //Thời gian lọc nhiễu nút nhấn.

// ==================================================
// 5. BIEN TRANG THAI HE THONG
// ==================================================
int soilRaw = 0; //giá trị ADC đọc trực tiếp từ biến trở, ví dụ 2048.
int soilPercent = 0; //giá trị độ ẩm đất đã quy đổi thành phần trăm, ví dụ 50.

float temperature = NAN; //nhiệt độ không khí.
float humidity = NAN; //độ ẩm không khí.

bool soilSensorReady = false; //ban đầu chương trình chưa xác nhận đã đọc cảm biến đất.
bool airSensorReady = false; //ban đầu chưa có dữ liệu DHT22 hợp lệ.

bool pumpOn = false; //ban đầu bơm ở trạng thái tắt.
bool autoMode = true; //ban đầu hệ thống ở chế độ tự động.

unsigned long pumpStartedAt = 0; //thời điểm bật bơm.
unsigned long lastPumpStoppedAt = 0; //thời điểm tắt bơm gần nhất.

unsigned long lastSoilReadAt = 0; //thời điểm đọc đất gần nhất.
unsigned long lastDHTReadAt = 0; //thời điểm đọc DHT22 gần nhất.
unsigned long lastLCDUpdateAt = 0; //thời điểm cập nhật LCD gần nhất.
unsigned long lastSerialPrintAt = 0; //thời điểm in trạng thái gần nhất.

bool lastModeButtonReading = HIGH; //lần đọc tín hiệu nút gần nhất.
bool stableModeButtonState = HIGH; //trạng thái nút đã được xác nhận ổn định.
unsigned long lastModeDebounceAt = 0; //thời điểm bắt đầu ghi nhận thay đổi tín hiệu.

bool lastPumpButtonReading = HIGH;
bool stablePumpButtonState = HIGH;
unsigned long lastPumpDebounceAt = 0;

// ==================================================
// 6. CAC MUC UU TIEN TUOI
// ==================================================

enum WaterPriority {
  PRIORITY_LOW,
  PRIORITY_NORMAL,
  PRIORITY_HIGH
};
//enum là kiểu dữ liệu liệt kê, cho phép tạo ra một nhóm giá trị có tên rõ ràng.

WaterPriority currentPriority = PRIORITY_NORMAL;

// ==================================================
// 7. TINH DO AM DAT
// ==================================================

int calculateSoilMoisture(int rawValue) {
  rawValue = constrain(rawValue, DRY_RAW, WET_RAW);
  int moisture = map(rawValue, DRY_RAW, WET_RAW, 0, 100);
  return constrain(moisture, 0, 100);
} 

// ==================================================
// 8. KIEM TRA DIEU KIEN BAT BOM
// ==================================================

bool canStartPump() {
  if (lastPumpStoppedAt == 0) {
    return true;
  }

  return millis() - lastPumpStoppedAt >= PUMP_COOLDOWN_MS;
}

// ==================================================
// 9. HAM BAT/TAT BOM
// ==================================================

void setPump(bool state) {
  if (state == pumpOn) {
    return;
  }

  // Khong cho bat bom neu chua het thoi gian nghi      CHUA HIEU
  if (state && !canStartPump()) {
    Serial.println(
      "Bom dang trong thoi gian nghi 5 giay."
    );
    return;
  }

  pumpOn = state;
  digitalWrite(PUMP_PIN, pumpOn ? HIGH : LOW);

  if (pumpOn) {
    pumpStartedAt = millis();
    Serial.println("BOM: BAT");
  } else {
    lastPumpStoppedAt = millis();
    Serial.println("BOM: TAT");
  }
}

// ==================================================
// 10. DOC CAM BIEN DO AM DAT
// ==================================================

void readSoilSensor() {
  soilRaw = analogRead(SOIL_PIN);
  soilPercent = calculateSoilMoisture(soilRaw);
  soilSensorReady = true;
}

// ==================================================
// 11. DOC DHT22
// ==================================================

void readAirSensor() {
  float newTemperature = dht.readTemperature(); //lấy nhiệt độ theo độ c
  float newHumidity = dht.readHumidity(); //lấy độ ẩm kk theo %

  if (!isnan(newTemperature) && !isnan(newHumidity) && newHumidity >= 0 && newHumidity <= 100) {
    bool firstValidReading = !airSensorReady;
    
    bool valueChanged = firstValidReading || fabs(newTemperature - temperature) >= 0.1 || fabs(newHumidity - humidity) >= 0.1;
    temperature = newTemperature; //temperature??
    humidity = newHumidity; //         CHUA HIEU
    airSensorReady = true;
    if (valueChanged) {
      Serial.print("Nhiet do: ");
      Serial.print(temperature, 1);
      Serial.print(" C | Do am khong khi: ");
      Serial.print(humidity, 1);
      Serial.println(" %");
    }
  } else {
    // Khong ghi de gia tri hop le cu bang NaN
    // He thong se tiep tuc dung gia tri hop le gan nhat.
    Serial.println("Canh bao: Doc DHT22 that bai.");
  }
}

// ==================================================
// 12. THUAT TOAN 1 + 2
// DANH GIA NHIET DO VA DO AM KHONG KHI
// ==================================================

WaterPriority evaluateWaterPriority() {
  // Chua co du lieu khong khi hop le:
  // khong dua ra ket luan thoi tiet.
  if (!airSensorReady || isnan(temperature) || isnan(humidity)) {
    return PRIORITY_NORMAL;
  }

  bool hotWeather = temperature >= TEMP_HOT;
  bool coolWeather = temperature <= TEMP_COOL;

  bool dryAir = humidity <= AIR_HUMIDITY_DRY;
  bool humidAir = humidity >= AIR_HUMIDITY_HIGH;

  if (hotWeather || dryAir) {
      return PRIORITY_HIGH;
  }

  // Mat va khong khi am:
  // muc uu tien thap hon.
  if (coolWeather && humidAir) {
    return PRIORITY_LOW;
  }

  return PRIORITY_NORMAL;
}

// ==================================================
// 13. CHU KY DOC DAT THEO MUC UU TIEN
// ==================================================

unsigned long getSoilReadInterval() {
  if (currentPriority == PRIORITY_HIGH) {
    return SOIL_INTERVAL_HIGH_MS;
  }

  if (currentPriority == PRIORITY_LOW) {
    return SOIL_INTERVAL_LOW_MS;
  }

  return SOIL_INTERVAL_NORMAL_MS;
}

// ==================================================
// 14. THUAT TOAN 3 + BO TONG HOP
// DIEU KHIEN BOM TU DONG
// ==================================================

void controlPump() {
  if (!autoMode || !soilSensorReady) {
    return;
  }

  currentPriority = evaluateWaterPriority();

  // Dat rat kho: can tuoi.
  if (soilPercent <= MOISTURE_ON) {
    if (!pumpOn) {
      setPump(true);
    }
  }

   // Dat du am: tat bom.
  else if (soilPercent >= MOISTURE_OFF) {
    if (pumpOn) {
      setPump(false);
    }
  }
}

// ==================================================
// 15. BAO VE BOM
// ==================================================

void checkPumpSafety() {
  if (pumpOn &&
      millis() - pumpStartedAt >= MAX_PUMP_RUN_MS) {

    setPump(false);

    Serial.println(
      "CANH BAO: Bom da tat sau 30 giay chay lien tuc."
    );
  }
}


// ==================================================
// 16. XU LY NUT NHAN
// ==================================================

void handleButtons() {
  unsigned long now = millis();

  // Nut AUTO / MANUAL
  bool modeReading = digitalRead(MODE_BUTTON_PIN);

  if (modeReading != lastModeButtonReading) {
    lastModeDebounceAt = now;
  }

  if (now - lastModeDebounceAt >= DEBOUNCE_MS) {
    if (modeReading != stableModeButtonState) {
      stableModeButtonState = modeReading;

      if (stableModeButtonState == LOW) {
        autoMode = !autoMode;

        Serial.print("Chuyen che do: ");
        Serial.println(autoMode ? "AUTO" : "MANUAL");

        if (!autoMode) {
          Serial.println(
            "MANUAL: Nguoi dung tu dieu khien bom."
          );
        }
      }
    }
  }

  lastModeButtonReading = modeReading;

  // Nut bat/tat bom thu cong
  bool pumpReading = digitalRead(PUMP_BUTTON_PIN);

  if (pumpReading != lastPumpButtonReading) {
    lastPumpDebounceAt = now;
  }

  if (now - lastPumpDebounceAt >= DEBOUNCE_MS) {
    if (pumpReading != stablePumpButtonState) {
      stablePumpButtonState = pumpReading;

      if (stablePumpButtonState == LOW) {
        if (!autoMode) {
          // Khong cho nguoi dung bat bom khi het
          // thoi gian nghi an toan.
          setPump(!pumpOn);
        } else {
          Serial.println(
            "Hay chuyen sang MANUAL de dieu khien bom."
          );
        }
      }
    }
  }

  lastPumpButtonReading = pumpReading;
}

// ==================================================
// 17. CAP NHAT LCD
// ==================================================

void printLCDLine(int row, String text) {
  // Giới hạn nội dung trong 16 ký tự
  if (text.length() > 16) {
    text = text.substring(0, 16);
  }

  // Thêm khoảng trắng để xóa các ký tự cũ còn dư
  while (text.length() < 16) {
    text += ' ';
  }

  // Ghi đè trực tiếp, không xóa dòng trước
  lcd.setCursor(0, row);
  lcd.print(text);
}

void updateLCD() {
  String line1 = "Dat:";

  if (soilSensorReady) {
    line1 += String(soilPercent) + "% ";
  } else {
    line1 += "--% ";
  }

  line1 += "T:";

  if (airSensorReady) {
    line1 += String(temperature, 1);
  } else {
    line1 += "--.-";
  }

  String line2 = autoMode ? "AUTO " : "MAN  ";
  line2 += "Bom:";
  line2 += pumpOn ? "ON" : "OFF";

  printLCDLine(0, line1);
  printLCDLine(1, line2);
}

// ==================================================
// 18. IN TRANG THAI SERIAL
// ==================================================

void printStatus() {
  Serial.println();
  Serial.println("========= TRANG THAI HE THONG =========");

  Serial.print("ADC dat: ");
  Serial.println(soilRaw);

  Serial.print("Do am dat: ");
  if (soilSensorReady) {
    Serial.print(soilPercent);
    Serial.println("%");
  } else {
    Serial.println("Chua co du lieu");
  }

  Serial.print("Nhiet do: ");
  if (airSensorReady) {
    Serial.print(temperature, 1);
    Serial.println(" C");
  } else {
    Serial.println("Chua co du lieu hop le");
  }

  Serial.print("Do am khong khi: ");
  if (airSensorReady) {
    Serial.print(humidity, 1);
    Serial.println("%");
  } else {
    Serial.println("Chua co du lieu hop le");
  }

  Serial.print("Muc uu tien: ");
  if (currentPriority == PRIORITY_HIGH) {
    Serial.println("CAO");
  } else if (currentPriority == PRIORITY_LOW) {
    Serial.println("THAP");
  } else {
    Serial.println("BINH THUONG");
  }

  Serial.print("Che do: ");
  Serial.println(autoMode ? "AUTO" : "MANUAL");

  Serial.print("May bom: ");
  Serial.println(pumpOn ? "BAT" : "TAT");

  // In thong bao tinh trang tuoi cay mot lan moi khung trang thai.
  if (autoMode && soilSensorReady) {
    if (soilPercent >= MOISTURE_OFF) {
      if (currentPriority == PRIORITY_HIGH) {
        Serial.println("///Dat du am: Chua tuoi, tiep tuc theo doi.");
      } else {
      Serial.println("Dat du am: Khong can tuoi.");
      }
    }
  }
  else if (soilPercent <= MOISTURE_ON) {
    if (currentPriority == PRIORITY_HIGH) {
        Serial.println("UU TIEN CAO: Dat kho, nguy co mat nuoc cao.");
    } else if (currentPriority == PRIORITY_LOW) {
        Serial.println("Dat kho: Van can tuoi du thoi tiet mat/am.");
    } else {Serial.println("Dat kho: Can tuoi.");}
  }
  else {
      Serial.println("Dat trong vung trung gian: Giu trang thai bom.");
  }
  Serial.print("WiFi: ");
  Serial.println(
    WiFi.status() == WL_CONNECTED
      ? "DA KET NOI"
      : "CHUA KET NOI"
  );
}

// ==================================================
// 19. GUI DU LIEU LEN THINGSPEAK
// ==================================================

void sendToThingSpeak() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("ThingSpeak: WiFi chua ket noi.");
    return;
  }

  if (!soilSensorReady || !airSensorReady) {
    Serial.println(
      "ThingSpeak: Chua du du lieu cam bien."
    );
    return;
  }

  String url = "http://api.thingspeak.com/update?api_key=";
  url += THINGSPEAK_WRITE_KEY;
  url += "&field1=" + String(soilPercent);
  url += "&field2=" + String(temperature, 1);
  url += "&field3=" + String(humidity, 1);
  url += "&field4=" + String(pumpOn ? 1 : 0);
  url += "&field5=" + String(autoMode ? 1 : 0);
  url += "&field6=" + String((int)currentPriority);

  HTTPClient http;
  http.setConnectTimeout(5000);
  http.setTimeout(5000);

  if (!http.begin(url)) {
    Serial.println("Khong khoi tao duoc HTTP.");
    return;
  }

  int httpCode = http.GET();

  Serial.print("ThingSpeak HTTP code: ");
  Serial.println(httpCode);

  if (httpCode > 0) {
    String response = http.getString();

    Serial.print("ThingSpeak response: ");
    Serial.println(response);

    if (httpCode == 200 && response.toInt() > 0) {
      Serial.println("Gui du lieu thanh cong.");
    } else {
      Serial.println(
        "ThingSpeak chua ghi du lieu. Kiem tra API key, "
        "channel va khoang cach gui."
      );
    }
  } else {
    Serial.print("Loi ket noi ThingSpeak: ");
    Serial.println(http.errorToString(httpCode));
  }

  http.end();
}

// ==================================================
// 20. TAC VU WIFI / THINGSPEAK RIENG
// ==================================================

void thingSpeakTask(void* parameter) {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD, 6);

  Serial.println("Tac vu WiFi dang chay.");

  for (;;) {
    if (WiFi.status() == WL_CONNECTED) {
      sendToThingSpeak();

      vTaskDelay(
        pdMS_TO_TICKS(THINGSPEAK_INTERVAL_MS)
      );
    } else {
      Serial.println("Dang cho ket noi WiFi...");

      vTaskDelay(pdMS_TO_TICKS(1000));
    }
  }
}

// ==================================================
// 21. SETUP
// ==================================================

void setup() {
  Serial.begin(115200);

  pinMode(PUMP_PIN, OUTPUT);
  pinMode(MODE_BUTTON_PIN, INPUT_PULLUP);
  pinMode(PUMP_BUTTON_PIN, INPUT_PULLUP);

  digitalWrite(PUMP_PIN, LOW);

  analogReadResolution(12);

  Wire.begin(LCD_SDA_PIN, LCD_SCL_PIN);
  lcd.init();
  lcd.backlight();

  lcd.setCursor(0, 0);
  lcd.print("HE THONG TUOI CAY");
  lcd.setCursor(0, 1);
  lcd.print("Dang khoi dong");

  dht.begin();

  Serial.println();
  Serial.println("=================================");
  Serial.println(" HE THONG TUOI CAY ESP32");
  Serial.println(" Che do mac dinh: AUTO");
  Serial.println("=================================");

  // Doc dat ngay khi khoi dong de khong dung
  // gia tri mac dinh 0% de bat bom nham.
  readSoilSensor();

  // Cho DHT22 co thoi gian on dinh truoc lan doc dau.
  // Khong delay dai de khong lam cham he thong.
  lastDHTReadAt = millis();

  // Tao tac vu ThingSpeak chay rieng.
  BaseType_t taskCreated = xTaskCreatePinnedToCore(
    thingSpeakTask,
    "ThingSpeakTask",
    8192,
    NULL,
    1,
    NULL,
    0
  );

  if (taskCreated == pdPASS) {
    Serial.println("Da tao tac vu ThingSpeak.");
  } else {
    Serial.println("Loi tao tac vu ThingSpeak.");
  }

  lcd.clear();
}

// ==================================================
// 22. LOOP
// ==================================================

void loop() {
  unsigned long now = millis();

  // Xu ly nut nhan lien tuc
  handleButtons();

  // Doc DHT22 moi 2 giay
  if (now - lastDHTReadAt >= DHT_INTERVAL_MS) {
    lastDHTReadAt = now;
    readAirSensor();

    // Cap nhat muc uu tien sau khi doc thoi tiet
    currentPriority = evaluateWaterPriority();
  }

  // Chu ky doc dat thay doi theo muc uu tien
  unsigned long soilInterval = getSoilReadInterval();

  if (now - lastSoilReadAt >= soilInterval) {
    lastSoilReadAt = now;
    readSoilSensor();
    controlPump();
  }

  // Bao ve bom luon duoc kiem tra
  checkPumpSafety();

  // Cap nhat LCD
  if (now - lastLCDUpdateAt >= LCD_INTERVAL_MS) {
    lastLCDUpdateAt = now;
    updateLCD();
  }

  // In trang thai
  if (now - lastSerialPrintAt >= SERIAL_INTERVAL_MS) {
    lastSerialPrintAt = now;
    printStatus();
  }

  // Nhuong thoi gian cho FreeRTOS
  delay(1);
}
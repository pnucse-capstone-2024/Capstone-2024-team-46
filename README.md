# IMU 센서를 활용한 AI 기반 온-디바이스 Anomaly Detection

## 1. 프로젝트 소개
### 1.1. 배경 및 필요성
 산업 현장에서의 생산, 제조 설비에서 이상이 발생할 경우, 공정이 멈추거나 불량이 발생하는 등 심각한 경제적 손실이 발생할 수 있다. 2021년 ISA에서 발표된 자료에 따르면 Fortune Global 500의 기업에서 연간 약 3.3백만 시간의 공정 중지상황이 발생한다. 이에 따른 손실액은 약 8640억 달러에 달한다. 
 
 이러한 상황을 방지하기 위해 이상 데이터를 수집, 분석하여 일반적인 상황과 이상 상황을 구분하는 것이 중요해졌다. 이상 감지 기술을 통해 설비 이상을 실시간으로 탐지할 수 있다면 설비 이상으로 인한 경제적 손실을 최소화할 수 있기 때문에 이상 감지 기술에 대한 수요가 높은 상황이다.	


### 1.2. 제품 목표
 PC의 시스템 펜에 대하여 이상 및 정상 데이터를 수집하여 AI모델에 학습시킨다. 학습시킨 모델을 토대로 시스템 펜에 이상이 발생했을 시, 이상치의 종류를 판별하여 웹 사이트에 경고창을 띄우고 시간과 어떤 종류의 이상이 발생했는지를 기록한다.
 <div align = "center">
    <img src="https://github.com/user-attachments/assets/24d2318d-51c4-4553-baca-2af9af81934a" alt="web_res" width="1000" />
</div>

 본 프로그램은 이상 탐지 및 분류 성능을 최적화하고 실시간으로 탐지하여, 설비의 이상유무를 확인한다. 공정 이상 상황으로 발생할 수 있는 잠재적 경제적 손실을 줄이기 위하여 설계되었다.

## 2. 시스템 구성도
![image](https://github.com/user-attachments/assets/4838f0ba-6000-4b7f-b6e6-57cec75dc578)

## 3. 상세설계
### 3.1. 데이터셋 수집
#### 3.1.1. 모델 최적화를 위한 산업 공정 오픈 데이터셋
![image](https://github.com/SmartManuAD/Smart-Manufacturing-AD/raw/main/Images/Logo_UPNA_SM.png)

UPNA 산업 공정 이상치 오픈 데이터셋을 사용하여 다양한 상황에 올바르게 동작하는 AI 모델을 설계하였다.

#### 3.1.2. PC 시스템 펜의 데이터셋 수집
본 프로젝트의 이상 감지 대상 기기인 PC의 시스템펜에서 정상 및 이상 데이터를 수집한다.
다음과 같은 정상 및 이상 상황을 가정하여 데이터를 수집한다.

##### 정상 시나리오
- 정상 시나리오는 기준 전압인 9V환경에서 수집되었으며, 산업 공정에서의 일반적인 노이즈를 모의실험하기 위하여 책상 주위를 가볍게 툭 툭 치거나 진동을 가하였다.

##### 이상 시나리오
1) 낮은 전압 환경: 기준 전압인 9v보다 낮은 전압인 3v와 5v를 인가하여 펜의 rpm을 떨어뜨린다.
2) 펜의 지면 접촉 불안정: 펜을 안정되지 않은 지면에 배치한다.
3) 펜에 이물질 삽입: 펜에 종이나 플라스틱과 같은 이물질을 직접 삽입한다.
4) 펜에 지속적인 이물질 접촉: 회전하는 펜에 지속적으로 부딛히도록 테이프를 붙인다.
5) 펜의 불균형: 펜의 날개 일부를 파손시키고 펜에 자석과 같은 무게가 있는 물체를 단다.
6) 펜의 갑작스런 종료: 동작하는 펜에 전원을 제거한다.

이상과 같은 시나리오를 통해 시스템 펜에서 다양한 패턴을 가진 진동 데이터를 수집하였다.

### 3.2 모델 구성도 
#### Anomaly detection 모델 구성도
![alt text](https://github.com/user-attachments/assets/28dfabfe-0976-4e22-9e96-7a1d723a59a3)


#### Anomaly Classification 모델 구성도
![alt text](https://github.com/user-attachments/assets/7ad83de6-345d-497d-bd40-d3332c2d0f6b)

<br>

### 3.3. 사용 모듈 임베디드 시스템
<div align = "center">
    <img src="https://docs.espressif.com/projects/esp-idf/en/stable/esp32/_images/esp32-DevKitM-1-isometric.png" alt="esp32" width="500" />
</div>
<div align = "center">
 ESP32-DevKitM-1
 </div>


<div align = "center">
    <img src="https://github.com/user-attachments/assets/550ff261-2c79-4373-be23-b4282965919e" alt="mpu-92/64" width="500" />
</div>
<div align = "center">
 진동 감지를 위한 가속도 센서 MPU-92/65
 </div>
<br>

<div align = "center">
    <img src="https://docs.espressif.com/projects/esp-idf/en/stable/esp32/_static/espressif-logo.svg" alt="espressif-logo" width="500" />
</div>
<div align = "center">
    <img src="https://github.com/user-attachments/assets/0e28a6f0-09a2-42ec-8b82-b008b7ca0a68" alt="esp-idf" width="500" />
</div>
<div align = "center">
 ESPRESSIF에서 제공하는 임베디드 시스템 개발을 위한 ESP-IDF
 </div>
<br>

가속도 센서에서 진동을 감지하여 해당하는 값을 ESP32에 포팅된 모델에 판별한다.
판별된 결과를 블루투스 통신으로 웹 페이지에 전송한다.

### 3.4. 결과 확인을 위한 웹 페이지 구현
<div align = "center">
    <img src="https://miro.medium.com/max/8400/1*kUcnzFjf1UJBKHE8oj5c6g.jpeg" alt="web-dev" width="700" />
</div>
<div align = "center">
 구현 환경
 </div>

##### 정상 판별 결과
<div align = "center">
    <img src="https://github.com/user-attachments/assets/e2c26118-796e-44eb-bac7-819136608069" alt="normal" width="700" />
</div>


##### 이상 판별 결과
<div align = "center">
    <img src="https://github.com/user-attachments/assets/6c245feb-e787-4b4f-9809-5804fdbfe0b9" alt="anomaly" width="700" />
</div>


## 4. 설치 가이드 

### 사전 준비

준비해야할 항목:
- Visual Studio Code (VSCode)
- ESP32-DevKitM-1 개발 보드
- ESP32-DevKitM-1을 PC와 연결할 USB 케이블


### 1단계: Visual Studio Code 설치

1. [Visual Studio Code 다운로드 페이지](https://code.visualstudio.com/)에 방문한다.
2. 운영 체제에 맞는 설치 파일을 다운로드한다. (Windows, macOS, Linux).
3. 다운로드한 설치 파일을 실행하고 화면의 지시에 따라 설치를 완료한다.

### 2단계: Visual Studio Code용 ESP-IDF 확장 프로그램 설치

1. Visual Studio Code를 연다.
2. 왼쪽 사이드바에서 네모난 아이콘을 클릭하거나 `Ctrl+Shift+X`를 눌러 확장 프로그램 보기로 이동한다.
3. "ESP-IDF"를 검색한 다음 `설치`를 클릭한다.

### 3단계: ESP32-DevKitM-1 모듈 연결

Anomaly Detection Prometheus 프로젝트는 ESP32-DevKitM-1 모듈 환경에서 개발되었으므로, 모듈을 USB 케이블로 PC와 연결한다.

만약 USB 장치에서 ESP32 모듈이 인식되지 않는다면, 아래 링크에서 드라이버를 다운로드하여 설치한다.:
[Silicon Labs USB to UART 브리지 드라이버](https://www.silabs.com/developers/usb-to-uart-bridge-vcp-drivers?tab=downloads)

### 4단계: ESP-IDF 프로젝트 빌드 및 플래시

1. ESP32 모듈이 정상적으로 인식되었다면, Visual Studio Code 내의 ESP-IDF CMD 터미널을 실행한다.
2. 프로젝트 폴더인 `Anomaly_Detection_Prometheus` 폴더로 이동한다.:
   ```bash
   cd Anomaly_Detection_Prometheus
    ```
3. 다음 명령어를 입력하여 프로젝트를 빌드하고, 플래시하고, 모니터링한다.:
    ```bash
    idf.py build flash monitor
    ```
4. 정상적으로 빌드가 완료되고 출력 로그가 정상적으로 보이면, 다음 단계로 진행한다.

### 5단계 : 웹 애플리케이션에서 BLE 연결
1. Anomaly Detection Notification.html 파일을 실행한다.
2. 웹 페이지에서 "Scan for BLE Devices" 버튼을 클릭하여 BLE 기기를 검색한다.
3. "ESP32-Prometheus" 기기에 연결하여 데이터 전송을 시작한다.

## 4. 소개 및 시연 영상
<div align = "center">
    <a href="https://youtu.be/_r0eA_bjf0g?si=xF_1aaH4oWfBnD5C">
        <img src="https://img.youtube.com/vi/_r0eA_bjf0g/0.jpg" alt="프로메테우스 팀 소개영상" width="500" />
    </a>
</div>

## 6. 구성원별 역할
| Name              | Contact Information     | Roles                                                   | 
|-------------------|-------------------------|---------------------------------------------------------|
| 김민재 | ysicka@gmail.com | 오픈 데이터셋 전처리 및 경량화, 시스템 팬 데이터 수집 및 학습 데이터 전처리 | 
| 최세영 | seyoung4503@gmail.com | 이상 탐지 및 분류 모델 설계와 성능 개선 | 
| 김경준 | kimkj0221@gmail.com | 모델 경량화 및 ESP32 펌웨어 개발, BLE를 이용한 웹 기반 사용자 인터페이스 구현 | 


## 7. 참고문헌
-  Herb Sutter, "The Free Lunch Is Over: A Fundamental Turn Toward Concurrency in Software," Dr. Dobb's Journal, March 2005.
-  John D. Owens, David Luebke, Naga Govindaraju, Mark Harris, Jens Krüger, Aaron E. Lefohn, and Timothy J. Purcell, "A Survey of General-Purpose Computation on Graphics Hardware," Computer Graphics Forum 26, no. 1 (March 2007): 80-113
- ISA, "World's Largest Manufacturers Lose Almost $1 Trillion Annually to Machine Failures," Automation.com, June 2021, https://www.automation.com/en-us/articles/june-2021/world-largest-manufacturers-lose-almost-1-trillion.
- SmartManuAD, "Smart Manufacturing AD: Open Datasets," GitHub repository, accessed October 16, 2024, https://github.com/SmartManuAD/Smart-Manufacturing-AD/tree/main.
- IEEE, "A Novel Approach for Anomaly Detection in Time Series Data Using a Hybrid Model," IEEE Access 10 (2022): 1055-1065, https://doi.org/10.1109/ACCESS.2022.9672830.
- Jun Shu, Zongben Xu, and Deyu Meng, "Small Sample Learning in Big Data Era," School of Mathematics and Statistics, Xi’an Jiaotong University, China, accessed October 16, 2024.


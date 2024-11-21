// Sử dụng vị trí của cửa sổ hiện tại làm URL cơ sở
const BASE_URL = window.location.origin;

const relays = ['motor', 'fan', 'lamp']; // Mảng các thiết bị cần điều khiển
let currentMode = 'MANUAL'; // Mặc định chế độ là 'MANUAL'

// Định nghĩa kiểu nút cho điều khiển relay
const buttonStyles = {
  show: 'mdl-button mdl-js-button mdl-button--raised mdl-js-ripple-effect mdl-button--accent',
  hidden: 'mdl-button mdl-js-button mdl-js-ripple-effect',
};

// Khởi tạo trạng thái relay và chế độ
document.addEventListener("DOMContentLoaded", () => {
    fetchInitialRelayStatus(); // Lấy trạng thái ban đầu của các thiết bị
    fetchSensorData(); // Lấy dữ liệu cảm biến
    updateRainStatus(); // Cập nhật trạng thái mưa

    // Thiết lập khoảng thời gian cập nhật dữ liệu cảm biến và trạng thái mưa
    setInterval(fetchSensorData, 10000); // Cập nhật cảm biến mỗi 10 giây
    setInterval(updateRainStatus, 10000); // Cập nhật trạng thái mưa mỗi 10 giây

    // Lắng nghe sự thay đổi chế độ điều khiển
    document.querySelectorAll('input[name="control"]').forEach((option) => {
        option.addEventListener('change', (event) => {
            currentMode = event.target.value.toUpperCase();
            const modeElement = document.getElementById('currentMode');
            if (modeElement) modeElement.innerText = `Chế độ hiện tại: ${currentMode === 'MANUAL' ? 'Thủ công' : currentMode === 'AUTO' ? 'Tự động' : 'Lịch trình'}`;
            setMode(currentMode); // Cập nhật chế độ cho backend
        });
    });

    // Lắng nghe sự kiện cho các nút điều khiển relay
    relays.forEach((relay) => {
        const onButton = document.querySelector(`[data-relay-name="${relay}_on-button"]`);
        const offButton = document.querySelector(`[data-relay-name="${relay}_off-button"]`);

        if (onButton && offButton) {
            onButton.addEventListener('click', () => controlRelay(relay, 'ON'));
            offButton.addEventListener('click', () => controlRelay(relay, 'OFF'));
        }
    });
});

// Hàm cập nhật chế độ trong backend
function setMode(mode) {
    fetch(`${BASE_URL}/setMode?mode=${mode}`)
        .then(response => response.text())
        .then(data => console.log(data))
        .catch(error => console.error('Error setting mode:', error));
}

// Hàm lấy trạng thái relay từ backend
async function fetchRelayStatus(relay) {
  try {
      const response = await fetch(`${BASE_URL}/${relay}/status`);
      if (!response.ok) throw new Error(`Error fetching ${relay} status: ${response.status}`);
      
      const status = await response.text();  // Trạng thái có thể là "ON" hoặc "OFF"
      console.log(`${relay} status: ${status}`);
      updateRelayUI(relay, status);  // Cập nhật UI theo trạng thái relay
  } catch (error) {
      console.error(`Error fetching ${relay} status:`, error);
  }
}

// Hàm cập nhật UI theo trạng thái relay (ON/OFF)
function updateRelayUI(relay, status) {
  const relayOnButton = document.querySelector(`[data-relay-name="${relay}_on-button"]`);
  const relayOffButton = document.querySelector(`[data-relay-name="${relay}_off-button"]`);
  const relayImage = document.getElementById(`${relay}-image`);

  if (relayOnButton && relayOffButton) {
      if (status === 'ON') {
          relayOnButton.setAttribute('class', buttonStyles.show);
          relayOffButton.setAttribute('class', buttonStyles.hidden);
      } else {
          relayOnButton.setAttribute('class', buttonStyles.hidden);
          relayOffButton.setAttribute('class', buttonStyles.show);
      }
  }

  if (relayImage) {
      relayImage.setAttribute('src', `${relay}_${status.toLowerCase()}.png`);  // Cập nhật hình ảnh theo trạng thái
  }
}

// Hàm điều khiển relay (bật/tắt)
async function controlRelay(relay, action) {
  try {
      const response = await fetch(`${BASE_URL}/${relay}?action=${action}`, { method: 'GET' });
      if (!response.ok) throw new Error(`Error controlling ${relay}: ${response.status}`);
      
      console.log(`${relay} turned ${action}`);
      await fetchRelayStatus(relay);  // Cập nhật lại trạng thái relay ngay lập tức
  } catch (error) {
      console.error(`Error controlling ${relay}:`, error);
  }
}

// Hàm cập nhật dữ liệu cảm biến
async function fetchSensorData() {
    try {
        const [tempResponse, soilResponse] = await Promise.all([
            fetch(`${BASE_URL}/temperature`),
            fetch(`${BASE_URL}/soil`)
        ]);

        if (!tempResponse.ok) throw new Error(`Temperature fetch error: ${tempResponse.status}`);
        if (!soilResponse.ok) throw new Error(`Soil moisture fetch error: ${soilResponse.status}`);

        const tempData = await tempResponse.json();
        const temperatureElement = document.getElementById("temperature");
        const humidityElement = document.getElementById("humidity");

        if (temperatureElement) temperatureElement.innerText = `${tempData.temperature} °C`;
        if (humidityElement) humidityElement.innerText = `${tempData.humidity} %`;

        const soilData = await soilResponse.json();
        const soilMoistureElement = document.getElementById("soilMoisture");

        if (soilMoistureElement) soilMoistureElement.innerText = `${soilData.soilMoisture} %`;
    } catch (error) {
        console.error('Error fetching sensor data:', error);
    }
}

// Hàm cập nhật trạng thái mưa
function updateRainStatus() {
    fetch(`${BASE_URL}/rain`)
        .then((response) => response.text())
        .then((status) => {
            const weatherElement = document.getElementById('weather');
            const weatherImageElement = document.getElementById('weather-image');

            if (weatherElement) weatherElement.innerText = status.toUpperCase() === 'RAIN' ? 'Mưa' : 'Nắng';
            if (weatherImageElement) {
                weatherImageElement.setAttribute('src', status.toUpperCase() === 'RAIN' ? 'rain.png' : 'sun.png');
            }
        })
        .catch((error) => {
            console.error('Error fetching rain status:', error);
        });
}

// Hàm lấy trạng thái ban đầu của các relay
async function fetchInitialRelayStatus() {
  for (let relay of relays) {
      await fetchRelayStatus(relay);  // Lấy trạng thái của từng relay
  }
}

// Hàm thiết lập thời gian cho máy bơm
function henGioMayBom() {
    fetch(`${BASE_URL}/checkScheduleMode`)
      .then(response => response.text())
      .then(data => {
        console.log("Schedule mode status:", data);

        if (data === "Schedule mode active") {
          var startTime = document.getElementById("pump-start-time").value;
          var endTime = document.getElementById("pump-end-time").value;

          if (!startTime || !endTime) {
            alert("Please enter valid start and end times.");
            return;
          }

          // Gửi thời gian bắt đầu và kết thúc đến server
          var url = `${BASE_URL}/setTimer?device=pump&startTime=${startTime}&endTime=${endTime}`;
          fetch(url)
            .then(response => {
              if (response.status === 200) {
                response.preventDefault();
                return response.text();
              } else {
                throw new Error("Failed to set timer.");
              }
            })
            .then(data => {
                alert(data);  // Hiển thị phản hồi từ server
            })
            .catch(error => {
                console.error("Error:", error);
                alert("Error: " + error.message);
            });
        } else {
          alert("Please enable schedule mode before setting timers.");
        }
      })
      .catch(error => {
        console.error("Error checking schedule mode:", error);
        alert("Error checking schedule mode.");
      });
}
function henGioQuat() {
  fetch(`${BASE_URL}/checkScheduleMode`)
    .then(response => response.text())
    .then(data => {
      console.log("Schedule mode status:", data);

      if (data === "Schedule mode active") {
        var startTime = document.getElementById("fan-start-time").value;
        var endTime = document.getElementById("fan-end-time").value;

        if (!startTime || !endTime) {
          alert("Please enter valid start and end times.");
          return;
        }

        // Gửi thời gian bắt đầu và kết thúc đến server
        var url = `${BASE_URL}/setTimer?device=fan&startTime=${startTime}&endTime=${endTime}`;
        fetch(url)
          .then(response => {
            if (response.status === 200) {
              response.preventDefault();
              return response.text();
            } else {
              throw new Error("Failed to set timer.");
            }
          })
          .then(data => {
              alert(data);  // Hiển thị phản hồi từ server
          })
          .catch(error => {
              console.error("Error:", error);
              alert("Error: " + error.message);
          });
      } else {
        alert("Please enable schedule mode before setting timers.");
      }
    })
    .catch(error => {
      console.error("Error checking schedule mode:", error);
      alert("Error checking schedule mode.");
    });
}

function henGioDen() {
  fetch(`${BASE_URL}/checkScheduleMode`)
    .then(response => response.text())
    .then(data => {
      console.log("Schedule mode status:", data);

      if (data === "Schedule mode active") {
        var startTime = document.getElementById("lamp-start-time").value;
        var endTime = document.getElementById("lamp-end-time").value;

        if (!startTime || !endTime) {
          alert("Please enter valid start and end times.");
          return;
        }

        // Gửi thời gian bắt đầu và kết thúc đến server
        var url = `${BASE_URL}/setTimer?device=lamp&startTime=${startTime}&endTime=${endTime}`;
        fetch(url)
          .then(response => {
            if (response.status === 200) {
              response.preventDefault();
              return response.text();
            } else {
              throw new Error("Failed to set timer.");
            }
          })
          .then(data => {
              alert(data);  // Hiển thị phản hồi từ server
          })
          .catch(error => {
              console.error("Error:", error);
              alert("Error: " + error.message);
          });
      } else {
        alert("Please enable schedule mode before setting timers.");
      }
    })
    .catch(error => {
      console.error("Error checking schedule mode:", error);
      alert("Error checking schedule mode.");
    });
}

function setTimer(event, device) {
  event.preventDefault(); // Prevent form submission

  const startTime = document.getElementById(`${device}-start-time`).value;
  const endTime = document.getElementById(`${device}-end-time`).value;

  if (!startTime || !endTime) {
    alert("Please enter valid start and end times.");
    return;
  }

  // Ensure schedule mode is active before setting timer
  fetch(`${BASE_URL}/checkScheduleMode`)
    .then(response => response.text())
    .then(data => {
      if (data === "Schedule mode active") {
        // Send the start and end times to the server
        const url = `${BASE_URL}/setTimer?device=${device}&startTime=${startTime}&endTime=${endTime}`;
        fetch(url)
          .then(response => {
            if (response.status === 200) {
              return response.text();
            } else {
              throw new Error("Failed to set timer.");
            }
          })
          .then(data => {
            alert(data);  // Display response from server
          })
          .catch(error => {
            console.error("Error:", error);
            alert("Error: " + error.message);
          });
      } else {
        alert("Please enable schedule mode before setting timers.");
      }
    })
    .catch(error => {
      console.error("Error checking schedule mode:", error);
      alert("Error checking schedule mode.");
    });
}

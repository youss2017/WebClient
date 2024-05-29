let socket;
let reconnectInterval = 2000; // 2 seconds
let totalPhysicalMemory = 1; // Initialize with a default value

function connectWebSocket() {
    socket = new WebSocket('ws://192.168.1.242:80/Stats');

    socket.onopen = function() {
        console.log('WebSocket connection established.');
        logMessage('WebSocket connection established.');
    };

    socket.onmessage = function(event) {
        const message = event.data.trim();

        if (message === 'refresh') {
            console.log('Refreshing the page...');
            window.location.reload(true); // Reload the page
            return;
        }

        try {
            const data = JSON.parse(message);
            const cpuUsage = data.cpu_usage;
            totalPhysicalMemory = data.total_physical_memory / 1e9; // Convert bytes to GB
            const usedMemory = data.total_used_memory / 1e9; // Convert bytes to GB

            // Update CPU usage chart
            cpuChart.data.datasets[0].data[0] = cpuUsage;
            cpuChart.update();

            // Update memory usage chart
            memoryChart.options.scales.y.max = totalPhysicalMemory;
            memoryChart.data.datasets[0].data[0] = usedMemory;
            memoryChart.update();
        } catch (e) {
            logMessage(message);
        }
    };


    socket.onerror = function(error) {
        console.error('WebSocket Error:', error);
        logMessage(`WebSocket Error: ${error}`);
    };

    socket.onclose = function() {
        console.log('WebSocket connection closed. Attempting to reconnect...');
        logMessage('WebSocket connection closed. Attempting to reconnect...');
        setTimeout(connectWebSocket, reconnectInterval);
    };

}

function logMessage(message) {
    const messageLog = document.getElementById('messageLog');
    messageLog.value += message + '\n';
    messageLog.scrollTop = messageLog.scrollHeight; // Auto-scroll to the bottom
}

document.getElementById('sendMessage').addEventListener('click', function() {
    sendMessage();
});

document.getElementById('messageInput').addEventListener('keypress', function(event) {
    if (event.key === 'Enter') {
        event.preventDefault(); // Prevent form submission
        sendMessage();
    }
});

function sendMessage() {
    const messageInput = document.getElementById('messageInput');
    const message = messageInput.value;
    const sendAnimation = document.getElementById('sendAnimation');
    const sendButton = document.getElementById('sendMessage');
    if (message && socket.readyState === WebSocket.OPEN) {
        // Show the sending animation
        sendAnimation.style.display = 'block';
        // Disable the button to prevent clicking while sending
        sendButton.disabled = true;
        socket.send(message);
        messageInput.value = ''; // Clear the input box
        // Hide the sending animation after 0.5 seconds (adjust as needed)
        setTimeout(() => {
            sendAnimation.style.display = 'none';
            // Re-enable the button after hiding the animation
            sendButton.disabled = false;
        }, 500);
    }
}

// Set up the charts
const cpuCtx = document.getElementById('cpuUsageChart').getContext('2d');
const memoryCtx = document.getElementById('memoryUsageChart').getContext('2d');

const chartOptions = {
    type: 'bar',
    options: {
        responsive: true,
        maintainAspectRatio: false, // Ensure the chart fills the chart box
        scales: {
            y: {
                beginAtZero: true,
                ticks: {
                    color: '#ffffff' // Y-axis label color for dark theme
                }
            },
            x: {
                ticks: {
                    color: '#ffffff' // X-axis label color for dark theme
                }
            }
        },
        plugins: {
            legend: {
                labels: {
                    color: '#ffffff' // Legend label color for dark theme
                }
            }
        }
    }
};

// Create CPU chart
const cpuChart = new Chart(cpuCtx, {
    ...chartOptions,
    data: {
        labels: ['CPU Usage'],
        datasets: [{
            label: 'CPU Usage (%)',
            data: [0],
            backgroundColor: ['rgba(75, 192, 192, 0.2)'],
            borderColor: ['rgba(75, 192, 192, 1)'],
            borderWidth: 1
        }]
    },
    options: {
        ...chartOptions.options,
        scales: {
            y: {
                beginAtZero: true,
                max: 100, // Set maximum y-axis value to 100%
                ticks: {
                    color: '#ffffff' // Y-axis label color for dark theme
                }
            },
            x: {
                ticks: {
                    color: '#ffffff' // X-axis label color for dark theme
                }
            }
        }
    }
});

// Create Memory chart
const memoryChart = new Chart(memoryCtx, {
    ...chartOptions,
    data: {
        labels: ['Used Memory'],
        datasets: [{
            label: 'Memory Usage (GB)',
            data: [0],
            backgroundColor: ['rgba(153, 102, 255, 0.2)'],
            borderColor: ['rgba(153, 102, 255, 1)'],
            borderWidth: 1
        }]
    }
});

// Function to update chart sizes
function updateChartSizes() {
    // Get the chart boxes
    const cpuChartBox = document.getElementById('cpuUsageChart').parentNode;
    const memoryChartBox = document.getElementById('memoryUsageChart').parentNode;

    // Set the chart canvas sizes to match the chart boxes
    cpuChart.canvas.style.width = cpuChartBox.offsetWidth + 'px';
    cpuChart.canvas.style.height = cpuChartBox.offsetHeight + 'px';

    memoryChart.canvas.style.width = memoryChartBox.offsetWidth + 'px';
    memoryChart.canvas.style.height = memoryChartBox.offsetHeight + 'px';

    // Update the charts
    cpuChart.update();
    memoryChart.update();
}

// Initial call to update chart sizes
updateChartSizes();

// Update chart sizes on window resize
window.addEventListener('resize', updateChartSizes);

// Function to update connection status
function updateConnectionStatus(status) {
    const connectionStatusElement = document.getElementById('connectionStatus');
    connectionStatusElement.innerHTML = `<img src="status.png" alt="Status" /> ${status}`;
}

// Call connectWebSocket to establish the WebSocket connection
connectWebSocket();

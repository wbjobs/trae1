window.updateChart = function (labels, datasets) {
    var ctx = document.getElementById('realtimeChart');
    if (!ctx) return;

    if (window.realtimeChartInstance) {
        window.realtimeChartInstance.data.labels = labels;
        window.realtimeChartInstance.data.datasets = datasets;
        window.realtimeChartInstance.update();
    } else {
        window.realtimeChartInstance = new Chart(ctx, {
            type: 'line',
            data: {
                labels: labels,
                datasets: datasets
            },
            options: {
                responsive: true,
                maintainAspectRatio: false,
                interaction: {
                    mode: 'index',
                    intersect: false
                },
                plugins: {
                    legend: {
                        display: true,
                        position: 'top'
                    }
                },
                scales: {
                    x: {
                    display: true,
                    title: {
                        display: true,
                        text: '时间'
                    }
                },
                    y: {
                        display: true,
                        title: {
                            display: true,
                            text: '数值'
                        }
                    }
                }
            }
        });
    }
};

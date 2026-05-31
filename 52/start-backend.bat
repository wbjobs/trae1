@echo off
echo Starting Industrial Sensor Monitor Backend...
cd backend
go mod tidy
go run main.go
pause

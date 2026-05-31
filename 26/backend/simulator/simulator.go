package simulator

import (
	"math"
	"math/rand"
	"time"

	"greenhouse-sim/model"
)

type Simulator struct {
	rng *rand.Rand
}

func New() *Simulator {
	return &Simulator{rng: rand.New(rand.NewSource(time.Now().UnixNano()))}
}

func (s *Simulator) Generate(zone model.Zone, t time.Time) model.SensorReading {
	hour := float64(t.Hour()) + float64(t.Minute())/60.0 + float64(t.Second())/3600.0

	temp := simulateTemperature(hour, zone)
	hum := simulateHumidity(hour, zone)
	light := simulateLight(hour, zone)
	co2 := simulateCO2(hour, zone)

	return model.SensorReading{
		ZoneID:      zone.ID,
		ZoneName:    zone.Name,
		Timestamp:   t.Unix(),
		Temperature: temp,
		Humidity:    hum,
		Light:       light,
		CO2:         co2,
	}
}

func (s *Simulator) jitter(base, pct float64) float64 {
	j := (s.rng.Float64()*2 - 1) * base * pct
	return base + j
}

func simulateTemperature(hour float64, zone model.Zone) float64 {
	base := 22.0
	switch zone.ID {
	case "zone-1":
		base = 24.0
	case "zone-2":
		base = 18.0
	case "zone-3":
		base = 26.0
	case "zone-4":
		base = 20.0
	case "zone-5":
		base = 22.0
	}
	amplitude := 6.0
	val := base + amplitude*math.Sin((hour-9)/24.0*2*math.Pi)
	val += (rand.Float64()*2 - 1) * 0.4
	return math.Round(val*100) / 100
}

func simulateHumidity(hour float64, zone model.Zone) float64 {
	base := 65.0
	switch zone.ID {
	case "zone-1":
		base = 70.0
	case "zone-2":
		base = 75.0
	case "zone-3":
		base = 80.0
	case "zone-4":
		base = 60.0
	case "zone-5":
		base = 68.0
	}
	amplitude := 15.0
	val := base + amplitude*math.Cos((hour-9)/24.0*2*math.Pi)
	val += (rand.Float64()*2 - 1) * 1.5
	if val < 20 {
		val = 20
	}
	if val > 100 {
		val = 100
	}
	return math.Round(val*100) / 100
}

func simulateLight(hour float64, zone model.Zone) float64 {
	sunrise := 6.0
	sunset := 19.0
	if hour < sunrise || hour > sunset {
		return math.Round((rand.Float64()*5) * 100) / 100
	}
	progress := (hour - sunrise) / (sunset - sunrise)
	val := 1000.0 * math.Sin(progress*math.Pi)
	switch zone.ID {
	case "zone-3":
		val *= 0.7
	case "zone-5":
		val *= 0.8
	}
	val += (rand.Float64()*2 - 1) * 30
	if val < 0 {
		val = 0
	}
	return math.Round(val*100) / 100
}

func simulateCO2(hour float64, zone model.Zone) float64 {
	base := 420.0
	switch zone.ID {
	case "zone-1":
		base = 450.0
	case "zone-2":
		base = 410.0
	case "zone-3":
		base = 480.0
	case "zone-4":
		base = 430.0
	case "zone-5":
		base = 440.0
	}
	amplitude := 80.0
	val := base + amplitude*math.Cos((hour-15)/24.0*2*math.Pi)
	val += (rand.Float64()*2 - 1) * 10
	if val < 300 {
		val = 300
	}
	if val > 1500 {
		val = 1500
	}
	return math.Round(val*100) / 100
}

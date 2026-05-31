package converter

import (
	"encoding/json"
	"fmt"

	"github.com/hamba/avro"
	jsoniter "github.com/json-iterator/go"
)

type Format string

const (
	FormatJSON Format = "json"
	FormatAvro Format = "avro"
)

type Converter struct {
	schemaCache map[string]avro.Schema
}

func New() *Converter {
	return &Converter{
		schemaCache: make(map[string]avro.Schema),
	}
}

func (c *Converter) parseSchema(schemaJSON string) (avro.Schema, error) {
	if s, ok := c.schemaCache[schemaJSON]; ok {
		return s, nil
	}
	s, err := avro.Parse(schemaJSON)
	if err != nil {
		return nil, fmt.Errorf("parse avro schema: %w", err)
	}
	c.schemaCache[schemaJSON] = s
	return s, nil
}

func (c *Converter) Convert(data []byte, sourceFmt, targetFmt, sourceSchema, targetSchema string) ([]byte, error) {
	if sourceFmt == targetFmt {
		return data, nil
	}

	switch sourceFmt {
	case string(FormatJSON):
		switch targetFmt {
		case string(FormatAvro):
			return c.jsonToAvro(data, targetSchema)
		}
	case string(FormatAvro):
		switch targetFmt {
		case string(FormatJSON):
			return c.avroToJSON(data, sourceSchema)
		}
	}
	return nil, fmt.Errorf("unsupported conversion: %s -> %s", sourceFmt, targetFmt)
}

func (c *Converter) jsonToAvro(jsonData []byte, schemaJSON string) ([]byte, error) {
	schema, err := c.parseSchema(schemaJSON)
	if err != nil {
		return nil, err
	}

	var record interface{}
	if err := jsoniter.Unmarshal(jsonData, &record); err != nil {
		return nil, fmt.Errorf("unmarshal json: %w", err)
	}

	avroBytes, err := avro.Marshal(schema, record)
	if err != nil {
		return nil, fmt.Errorf("marshal avro: %w", err)
	}
	return avroBytes, nil
}

func (c *Converter) avroToJSON(avroData []byte, schemaJSON string) ([]byte, error) {
	schema, err := c.parseSchema(schemaJSON)
	if err != nil {
		return nil, err
	}

	var record interface{}
	if err := avro.Unmarshal(schema, avroData, &record); err != nil {
		return nil, fmt.Errorf("unmarshal avro: %w", err)
	}

	jsonBytes, err := json.Marshal(record)
	if err != nil {
		return nil, fmt.Errorf("marshal json: %w", err)
	}
	return jsonBytes, nil
}

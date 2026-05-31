package converter

import (
	"encoding/json"
	"fmt"
	"time"

	"go.mongodb.org/mongo-driver/bson"
	"go.mongodb.org/mongo-driver/bson/primitive"
)

func ConvertBSONToClickHouse(doc bson.M, primaryKey, versionField string) (map[string]interface{}, error) {
	result := make(map[string]interface{})
	now := time.Now()

	if oid, ok := doc["_id"].(primitive.ObjectID); ok {
		result[primaryKey] = oid.Hex()
	} else if doc["_id"] != nil {
		result[primaryKey] = doc["_id"]
	}

	for key, value := range doc {
		if key == "_id" {
			continue
		}
		converted, err := convertValue(value)
		if err != nil {
			return nil, fmt.Errorf("failed to convert key %s: %w", key, err)
		}
		result[key] = converted
	}

	if versionField != "" {
		result[versionField] = now.UnixMilli()
	}

	return result, nil
}

func convertValue(v interface{}) (interface{}, error) {
	switch val := v.(type) {
	case primitive.ObjectID:
		return val.Hex(), nil
	case primitive.DateTime:
		return val.Time(), nil
	case primitive.Timestamp:
		return time.Unix(int64(val.T), 0), nil
	case primitive.Decimal128:
		return val.String(), nil
	case primitive.Regex:
		return val.Pattern, nil
	case primitive.Binary:
		return val.Data, nil
	case bson.M:
		jsonBytes, err := json.Marshal(val)
		if err != nil {
			return nil, err
		}
		return string(jsonBytes), nil
	case bson.A:
		jsonBytes, err := json.Marshal(val)
		if err != nil {
			return nil, err
		}
		return string(jsonBytes), nil
	case []interface{}:
		jsonBytes, err := json.Marshal(val)
		if err != nil {
			return nil, err
		}
		return string(jsonBytes), nil
	case float64, float32, int, int64, int32, bool, string:
		return val, nil
	case nil:
		return nil, nil
	default:
		jsonBytes, err := json.Marshal(val)
		if err != nil {
			return fmt.Sprintf("%v", val), nil
		}
		return string(jsonBytes), nil
	}
}

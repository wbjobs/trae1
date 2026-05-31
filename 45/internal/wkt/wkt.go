package wkt

import (
	"fmt"
	"time"

	"github.com/jhump/protoreflect/desc"
	"google.golang.org/protobuf/types/descriptorpb"
)

type FieldPathErr struct {
	Path    string
	Message string
	Example string
}

func (e *FieldPathErr) Error() string {
	if e.Example != "" {
		return fmt.Sprintf("%s (field: %s)\nexpected format example: %s", e.Message, e.Path, e.Example)
	}
	return fmt.Sprintf("%s (field: %s)", e.Message, e.Path)
}

const (
	TimestampFullName = "google.protobuf.Timestamp"
	DurationFullName  = "google.protobuf.Duration"
	StructFullName    = "google.protobuf.Struct"
	ValueFullName     = "google.protobuf.Value"
	ListValueFullName = "google.protobuf.ListValue"
	AnyFullName       = "google.protobuf.Any"
	FieldMaskFullName = "google.protobuf.FieldMask"
	EmptyFullName     = "google.protobuf.Empty"
)

func IsWKT(fullName string) bool {
	switch fullName {
	case TimestampFullName, DurationFullName, StructFullName, ValueFullName,
		ListValueFullName, AnyFullName, FieldMaskFullName, EmptyFullName:
		return true
	}
	return false
}

func TransformRequest(msgType *desc.MessageDescriptor, in interface{}, path string) (interface{}, error) {
	if in == nil {
		return nil, nil
	}
	if IsWKT(msgType.GetFullyQualifiedName()) {
		return transformWKTValue(msgType.GetFullyQualifiedName(), in, path)
	}
	switch v := in.(type) {
	case map[string]interface{}:
		return transformMessage(msgType, v, path)
	case []interface{}:
		return transformList(msgType, v, path)
	default:
		return in, nil
	}
}

func transformMessage(msgType *desc.MessageDescriptor, m map[string]interface{}, path string) (map[string]interface{}, error) {
	if m == nil {
		return nil, nil
	}
	out := make(map[string]interface{}, len(m))
	fieldByName := map[string]*desc.FieldDescriptor{}
	for _, f := range msgType.GetFields() {
		fieldByName[f.GetName()] = f
		fieldByName[f.GetJSONName()] = f
	}
	for k, v := range m {
		fd, ok := fieldByName[k]
		if !ok {
			out[k] = v
			continue
		}
		subPath := joinPath(path, fd.GetName())
		transformed, err := transformField(fd, v, subPath)
		if err != nil {
			return nil, err
		}
		out[k] = transformed
	}
	return out, nil
}

func transformField(fd *desc.FieldDescriptor, v interface{}, path string) (interface{}, error) {
	if v == nil {
		return nil, nil
	}
	switch fd.GetType() {
	case descriptorpb.FieldDescriptorProto_TYPE_MESSAGE:
		mt := fd.GetMessageType()
		if fd.IsMap() {
			return v, nil
		}
		if fd.IsRepeated() {
			arr, ok := v.([]interface{})
			if !ok {
				return nil, &FieldPathErr{Path: path, Message: fmt.Sprintf("expected array for repeated field %q, got %T", fd.GetName(), v)}
			}
			out := make([]interface{}, len(arr))
			for i, item := range arr {
				t, err := TransformRequest(mt, item, fmt.Sprintf("%s[%d]", path, i))
				if err != nil {
					return nil, err
				}
				out[i] = t
			}
			return out, nil
		}
		return TransformRequest(mt, v, path)
	default:
		return v, nil
	}
}

func transformList(msgType *desc.MessageDescriptor, arr []interface{}, path string) ([]interface{}, error) {
	out := make([]interface{}, len(arr))
	for i, item := range arr {
		t, err := TransformRequest(msgType, item, fmt.Sprintf("%s[%d]", path, i))
		if err != nil {
			return nil, err
		}
		out[i] = t
	}
	return out, nil
}

func transformWKTValue(fullName string, v interface{}, path string) (interface{}, error) {
	switch fullName {
	case TimestampFullName:
		return transformTimestampValue(v, path)
	case DurationFullName:
		return transformDurationValue(v, path)
	case StructFullName:
		return v, nil
	case ValueFullName:
		return v, nil
	case ListValueFullName:
		return v, nil
	case AnyFullName:
		return v, nil
	case FieldMaskFullName:
		return v, nil
	case EmptyFullName:
		return map[string]interface{}{}, nil
	}
	return v, nil
}

func transformTimestampValue(v interface{}, path string) (interface{}, error) {
	switch t := v.(type) {
	case string:
		if _, err := time.Parse(time.RFC3339Nano, t); err != nil {
			return nil, &FieldPathErr{
				Path:    path,
				Message: fmt.Sprintf("invalid RFC3339 timestamp %q for google.protobuf.Timestamp: %v", t, err),
				Example: `"createdAt": "2024-01-02T15:04:05Z"`,
			}
		}
		return t, nil
	case float64:
		if t == float64(int64(t)) {
			return time.Unix(int64(t), 0).UTC().Format(time.RFC3339Nano), nil
		}
		return nil, &FieldPathErr{
			Path:    path,
			Message: fmt.Sprintf("google.protobuf.Timestamp expects RFC3339 string or unix seconds, got float %v", t),
			Example: `"createdAt": "2024-01-02T15:04:05Z"`,
		}
	case map[string]interface{}:
		for _, key := range []string{"seconds", "nanos"} {
			if _, has := t[key]; !has {
				return nil, &FieldPathErr{
					Path:    path,
					Message: fmt.Sprintf("google.protobuf.Timestamp expects RFC3339 string or object with seconds+nanos; missing %q", key),
					Example: `"createdAt": "2024-01-02T15:04:05Z" or "createdAt": {"seconds": 1704162245, "nanos": 0}`,
				}
			}
		}
		return t, nil
	default:
		return nil, &FieldPathErr{
			Path:    path,
			Message: fmt.Sprintf("google.protobuf.Timestamp expects RFC3339 string or {seconds, nanos} object, got %T", v),
			Example: `"createdAt": "2024-01-02T15:04:05Z"`,
		}
	}
}

func transformDurationValue(v interface{}, path string) (interface{}, error) {
	switch t := v.(type) {
	case string:
		if _, err := time.ParseDuration(t); err != nil {
			return nil, &FieldPathErr{
				Path:    path,
				Message: fmt.Sprintf("invalid Go-style duration %q for google.protobuf.Duration: %v", t, err),
				Example: `"ttl": "30s" (supported: "1h30m", "500ms", "2s")`,
			}
		}
		return t, nil
	case map[string]interface{}:
		for _, key := range []string{"seconds", "nanos"} {
			if _, has := t[key]; !has {
				return nil, &FieldPathErr{
					Path:    path,
					Message: fmt.Sprintf("google.protobuf.Duration expects duration string or object with seconds+nanos; missing %q", key),
					Example: `"ttl": "30s" or "ttl": {"seconds": 30, "nanos": 0}`,
				}
			}
		}
		return t, nil
	default:
		return nil, &FieldPathErr{
			Path:    path,
			Message: fmt.Sprintf("google.protobuf.Duration expects duration string or {seconds, nanos} object, got %T", v),
			Example: `"ttl": "30s"`,
		}
	}
}

func joinPath(a, b string) string {
	if a == "" {
		return b
	}
	return a + "." + b
}

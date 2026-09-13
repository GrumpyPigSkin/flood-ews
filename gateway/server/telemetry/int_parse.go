// JSON is all parsed as if it is float64 values by the default un-marshaller To get
// around this this custom parser, parses the valid integer values to save from
// decoding errors; the same issue as described here:
// https://stackoverflow.com/questions/22343083/json-unmarshaling-with-long-numbers-gives-floating-point-number
package telemetry

import (
	"encoding/json"
	"fmt"
	"strconv"
)

// Underlying type
type FlexUint uint64

// Unmarshall the value
func (f *FlexUint) UnmarshalJSON(data []byte) error {
	if string(data) == "null" {
		return nil
	}

	// Try unmarshall from a string
	var str string
	if err := json.Unmarshal(data, &str); err == nil {
		val, err := strconv.ParseUint(str, 10, 64)
		if err != nil {
			return fmt.Errorf("invalid uint string: %w", err)
		}
		*f = FlexUint(val)
		return nil
	}

	// Try unmarshall from a float64 to a uint64
	var num float64
	if err := json.Unmarshal(data, &num); err == nil {
		*f = FlexUint(num)
		return nil
	}

	// Fallback
	return fmt.Errorf("cannot unmarshal %s into FlexUint", data)
}

// Conversion functions
func (f FlexUint) Uint64() uint64 { return uint64(f) }
func (f FlexUint) Uint32() uint32 { return uint32(f) }
func (f FlexUint) Uint16() uint16 { return uint16(f) }
func (f FlexUint) Uint8() uint8   { return uint8(f) }
func (f FlexUint) Int() int       { return int(f) }

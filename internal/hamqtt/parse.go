package hamqtt

import "encoding/json"

// ParseState reports whether body is a Home Assistant snapshot object with a
// "phase" key. zoneIDs come from zones[].id; a missing zones array is ok.
func ParseState(body []byte) (zoneIDs []string, ok bool) {
	var raw any
	if err := json.Unmarshal(body, &raw); err != nil {
		return nil, false
	}
	obj, isObj := raw.(map[string]any)
	if !isObj {
		return nil, false
	}
	if _, has := obj["phase"]; !has {
		return nil, false
	}
	zones, hasZones := obj["zones"]
	if !hasZones {
		return nil, true
	}
	arr, isArr := zones.([]any)
	if !isArr {
		return nil, true
	}
	for _, z := range arr {
		zm, isMap := z.(map[string]any)
		if !isMap {
			continue
		}
		id, _ := zm["id"].(string)
		if id != "" {
			zoneIDs = append(zoneIDs, id)
		}
	}
	return zoneIDs, true
}

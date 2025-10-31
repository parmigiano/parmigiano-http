package util

import "strings"

func ExtractKeyFromURL(url string) string {
	parts := strings.SplitN(url, ".cloud/", 2)
	if len(parts) < 2 {
		return ""
	}

	return parts[1]
}

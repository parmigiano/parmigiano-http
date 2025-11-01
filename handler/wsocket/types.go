package wsocket

import (
	"sync"

	"github.com/gorilla/websocket"
)

type Client struct {
	Conn *websocket.Conn
}

type Hub struct {
	mu      sync.Mutex
	clients map[*Client]bool
}

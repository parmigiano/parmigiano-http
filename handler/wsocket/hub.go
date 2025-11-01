package wsocket

import (
	"fmt"
	"sync"
)

var hubInstance *Hub
var once sync.Once

func GetHub() *Hub {
	once.Do(func() {
		hubInstance = &Hub{
			clients: make(map[*Client]bool),
		}
	})

	return hubInstance
}

func (h *Hub) AddClient(c *Client) {
	h.mu.Lock()
	h.clients[c] = true
	h.mu.Unlock()

	fmt.Printf("[INFO] Client connected by wsocket: %s\n", c.Conn.RemoteAddr().String())
}

func (h *Hub) RemoveClient(c *Client) {
	h.mu.Lock()
	delete(h.clients, c)
	h.mu.Unlock()
}

func (h *Hub) Broadcast(message interface{}) {
	h.mu.Lock()
	defer h.mu.Unlock()

	for c := range h.clients {
		err := c.Conn.WriteJSON(message)
		if err != nil {
			c.Conn.Close()
			delete(h.clients, c)
		}
	}
}

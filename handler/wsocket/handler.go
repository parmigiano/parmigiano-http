package wsocket

import (
	"net/http"
	"parmigiano/http/infra/logger"

	"github.com/gorilla/websocket"
)

var log *logger.Logger = logger.NewLogger()

var upgrader = websocket.Upgrader{
	CheckOrigin: func(r *http.Request) bool {
		return true
	},
}

func HandleWebSocket(w http.ResponseWriter, r *http.Request) {
	conn, err := upgrader.Upgrade(w, r, nil)
	if err != nil {
		log.Error(err.Error())
		return
	}

	client := &Client{Conn: conn}
	hub := GetHub()

	hub.AddClient(client)

	defer func() {
		hub.RemoveClient(client)
		conn.Close()
	}()

	for {
		var msg map[string]interface{}
		if err := conn.ReadJSON(&msg); err != nil {
			break
		}
	}
}

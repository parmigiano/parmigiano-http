package wsocket

import (
	"database/sql"
	"net/http"
	"parmigiano/http/infra/constants"
	"parmigiano/http/infra/logger"
	"parmigiano/http/infra/store/postgres/store"
	"strconv"

	"github.com/gorilla/websocket"
)

func NewWSHandler(db *sql.DB, log *logger.Logger, store store.Storage) *WSHandler {
	return &WSHandler{
		Db:     db,
		Logger: log,
		Store:  store,
	}
}

var upgrader = websocket.Upgrader{
	CheckOrigin: func(r *http.Request) bool {
		return true
	},
}

// ?uid=123
func (h *WSHandler) HandleWebSocket(w http.ResponseWriter, r *http.Request) {
	uidParam := r.URL.Query().Get("uid")
	uid, _ := strconv.ParseUint(uidParam, 10, 64)

	conn, err := upgrader.Upgrade(w, r, nil)
	if err != nil {
		h.Logger.Error(err.Error())
		return
	}

	client := &Client{Conn: conn, UserUid: uid}

	hub := GetHub()
	hub.AddClient(client)

	defer func() {
		hub.RemoveClient(client)
		conn.Close()
	}()

	// online
	hub.Broadcast(map[string]any{
		"event": constants.EVENT_USER_ONLINE,
		"data": map[string]any{
			"user_uid": uid,
			"online":   true,
		},
	})

	// offline
	defer func() {
		hub.RemoveClient(client)
		conn.Close()

		hub.Broadcast(map[string]any{
			"event": constants.EVENT_USER_ONLINE,
			"data": map[string]any{
				"user_uid": uid,
				"online":   false,
			},
		})
	}()

	for {
		var msg map[string]interface{}
		if err := conn.ReadJSON(&msg); err != nil {
			break
		}

		// processing req.
		h.handleIncomingRequest(client, msg)
	}
}

package wsocket

import (
	"database/sql"
	"parmigiano/http/infra/logger"
	"parmigiano/http/infra/store/postgres/store"
	"sync"

	"github.com/gorilla/websocket"
)

type WSHandler struct {
	Db     *sql.DB
	Logger *logger.Logger
	Store  store.Storage
}

type Hub struct {
	mu      sync.Mutex
	clients map[*Client]bool
}

type Client struct {
	UserUid uint64
	Conn    *websocket.Conn
	Send    chan interface{}
}

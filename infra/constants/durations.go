package constants

import "time"

const REDIS_POOL_TIMEOUT time.Duration = 17 * time.Second
const REDIS_CONN_MAX_IDLE_TIME time.Duration = 5 * time.Minute

const SERVER_READ_TIMEOUT time.Duration = 5 * time.Minute
const SERVER_WRITE_TIMEOUT time.Duration = 5 * time.Minute
const SERVER_IDLE_TIMEOUT time.Duration = 90 * time.Second

const EMAIL_LINK_TIMEOUT time.Duration = 30 * time.Minute

package auth

import (
	"context"
	"fmt"
	"math/rand"
	"parmigiano/http/handler/wsocket"
	"parmigiano/http/infra/constants"
	"parmigiano/http/infra/encryption"

	"net/http"
	"parmigiano/http/infra/store/postgres/models"
	"parmigiano/http/pkg/httpx"
	"parmigiano/http/pkg/httpx/httperr"
	"parmigiano/http/util"
	"time"

	"github.com/go-playground/validator"
)

// AuthCreateUserHandler инициализация пользователя
func (h *Handler) AuthCreateUserHandler(w http.ResponseWriter, r *http.Request) error { //nolint
	ctx := r.Context()

	var payload *AuthCreatePayload

	if err := httpx.HttpParse(r, &payload); err != nil {
		h.Logger.Error("%v", err)
		return httperr.BadRequest(err.Error())
	}

	if err := httpx.Validate.Struct(payload); err != nil {
		h.Logger.Error("%v", err)
		if _, ok := err.(validator.ValidationErrors); ok {
			return httperr.BadRequest(httpx.ValidateMsg(err))
		}

		return httperr.BadRequest("не все поля заполнены")
	}

	token := util.HashTo255(fmt.Sprintf("%s:%s:%s", payload.Username, payload.Email, time.Now().String()))

	tx, err := h.Db.BeginTx(ctx, nil)
	if err != nil {
		h.Logger.Error("%v", err)
		return httperr.Db(ctx, httperr.Err_DbNetwork)
	}

	defer func() {
		_ = tx.Rollback()
	}()

	rnd := rand.New(rand.NewSource(time.Now().UnixNano()))
	uid := rnd.Intn(9000000000) + 1000000000

	pass, err := encryption.Encrypt(payload.Password)
	if err != nil {
		h.Logger.Error("%v", err)
		return httperr.InternalServerError(err.Error())
	}

	userCore := &models.UserCore{
		UserUid:     uint64(uid),
		Email:       payload.Email,
		Password:    pass,
		AccessToken: token,
	}

	if errUserCore := h.Store.Users.Create_UserCore(tx, ctx, userCore); errUserCore != nil {
		h.Logger.Error("%v", errUserCore)
		return httperr.Db(ctx, errUserCore)
	}

	UserProfileModel := &models.UserProfile{
		UserUid:  uint64(uid),
		Avatar:   nil,
		Username: payload.Username,
	}

	if err := h.Store.Users.Create_UserProfile(tx, ctx, UserProfileModel); err != nil {
		h.Logger.Error("%v", err)
		return httperr.Db(ctx, err)
	}

	UserActiveModel := &models.UserActive{
		UserUid: uint64(uid),
	}

	if err := h.Store.Users.Create_UserActive(tx, ctx, UserActiveModel); err != nil {
		h.Logger.Error("%v", err)
		return httperr.Db(ctx, err)
	}

	if err := tx.Commit(); err != nil {
		h.Logger.Error("%v", err)
		return httperr.Conflict("failed to save data, please try again later")
	}

	// send event 'register_new_user' for all users
	go func(userUid uint64) {
		user, err := h.Store.Users.Get_UserWithLMessage(context.Background(), userUid)
		if err != nil {
			h.Logger.Error("%v", err)
			return
		}

		fmt.Println(user)

		hub := wsocket.GetHub()
		hub.Broadcast(map[string]any{
			"event": constants.EVENT_USER_NEW_REGISTER,
			"data":  user,
		})
	}(UserActiveModel.UserUid)

	httpx.HttpResponse(w, r, http.StatusCreated, token)
	return nil
}

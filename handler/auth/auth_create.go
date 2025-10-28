package auth

import (
	"fmt"
	"parmigiano/http/infra/encryption"

	"net/http"
	"parmigiano/http/infra/store/postgres/models"
	"parmigiano/http/pkg/httpx"
	"parmigiano/http/pkg/httpx/httperr"
	"parmigiano/http/util"
	"time"

	"github.com/go-playground/validator"
	"github.com/google/uuid"
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

	uuid := uuid.NewString()
	pass, err := encryption.Encrypt(payload.Password)
	if err != nil {
		h.Logger.Error("%v", err)
		return httperr.InternalServerError(err.Error())
	}

	userCore := &models.UserCore{
		UserUUID:    uuid,
		Email:       payload.Email,
		Password:    pass,
		AccessToken: token,
	}

	if errUserCore := h.Store.Users.Create_UserCore(tx, ctx, userCore); errUserCore != nil {
		h.Logger.Error("%v", errUserCore)
		return httperr.Db(ctx, errUserCore)
	}

	UserProfileModel := &models.UserProfile{
		UserUUID: uuid,
		Avatar:   nil,
		Username: payload.Username,
	}

	if err := h.Store.Users.Create_UserProfile(tx, ctx, UserProfileModel); err != nil {
		h.Logger.Error("%v", err)
		return httperr.Db(ctx, err)
	}

	if err := tx.Commit(); err != nil {
		h.Logger.Error("%v", err)
		return httperr.Conflict("failed to save data, please try again later")
	}

	httpx.HttpResponse(w, r, http.StatusCreated, token)
	return nil
}

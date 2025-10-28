package auth

import (
	"fmt"
	"parmigiano/http/infra/constants"

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

		return httperr.BadRequest("not all fields are filled")
	}

	token := util.HashTo255(fmt.Sprintf("%s:%s", payload.Email, time.Now().String()))

	tx, err := h.Db.BeginTx(ctx, nil)
	if err != nil {
		h.Logger.Error("%v", err)
		return httperr.Db(ctx, httperr.Err_DbNetwork)
	}

	defer func() {
		_ = tx.Rollback()
	}()

	uuid := uuid.NewString()

	userCore := &models.UserCore{
		UserUUID:    uuid,
		Email:       payload.Email,
		AccessToken: token,
	}

	if errUserCore := h.Store.Users.Create_UserCore(ctx, tx, userCore); errUserCore != nil {
		h.Logger.Error("%v", errUserCore)
		return httperr.Db(ctx, errUserCore)
	}

	userSubscriptionModel := &models.UserSubscription{
		UserUUID: uuid,
		PlanID:   constants.Free_Index,
		IsActive: false,
	}

	if err := h.Store.Users.Create_UserSubscription(ctx, tx, userSubscriptionModel); err != nil {
		h.Logger.Error("%v", err)
		return httperr.Db(ctx, err)
	}

	if err := h.Store.Users.Create_UserUsage(ctx, tx, uuid); err != nil {
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

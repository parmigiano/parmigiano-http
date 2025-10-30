package auth

import (
	"parmigiano/http/infra/encryption"

	"net/http"
	"parmigiano/http/pkg/httpx"
	"parmigiano/http/pkg/httpx/httperr"

	"github.com/go-playground/validator"
)

// AuthCreateUserHandler инициализация пользователя
func (h *Handler) AuthLoginUserHandler(w http.ResponseWriter, r *http.Request) error { //nolint
	ctx := r.Context()

	var payload *AuthLoginPayload

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

	pass, err := encryption.Encrypt(payload.Password)
	if err != nil {
		h.Logger.Error("%v", err)
		return httperr.InternalServerError(err.Error())
	}

	user, err := h.Store.Users.Get_UserCoreByEmail(ctx, payload.Email)
	if err != nil {
		h.Logger.Error("%v", err)
		return httperr.Db(ctx, err)
	}

	if user == nil {
		return httperr.NotFound("пользователь не был найден")
	}

	if user.Password != pass {
		return httperr.NotFound("пользователь не был найден")
	}

	httpx.HttpResponse(w, r, http.StatusOK, user.AccessToken)
	return nil
}

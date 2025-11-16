package users

import (
	"net/http"
	"parmigiano/http/infra/store/postgres/models"
	"parmigiano/http/pkg/httpx"
	"parmigiano/http/pkg/httpx/httperr"
	"parmigiano/http/pkg/security"
	"parmigiano/http/types"
	"regexp"
	"strings"

	"github.com/go-playground/validator"
)

func (h *Handler) UserUpdateProfile(w http.ResponseWriter, r *http.Request) error {
	ctx := r.Context()
	authToken := ctx.Value("identity").(*types.AuthToken)

	var payload *UserUpdateProfilePayload

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

	valid := regexp.MustCompile(`^[a-zA-Z0-9_]+$`)
	if payload.Name != nil && !valid.MatchString(*payload.Name) {
		return httperr.BadRequest("недопустимые символы в имени")
	}

	if payload.Username != nil && !valid.MatchString(*payload.Username) {
		return httperr.BadRequest("недопустимые символы в имени пользователя")
	}

	var emailPtr, passwordPtr *string

	if payload.Email != nil && strings.TrimSpace(*payload.Email) != "" {
		email := strings.ToLower(strings.TrimSpace(*payload.Email))
		emailPtr = &email
	}

	if payload.Password != nil && strings.TrimSpace(*payload.Password) != "" {
		password := strings.TrimSpace(*payload.Password)

		pass, err := security.HashPassword(password)
		if err != nil {
			h.Logger.Error("%v", err)
			return httperr.InternalServerError("ошибка при обновлении пароля")
		}

		passwordPtr = &pass
	}

	tx, err := h.Db.BeginTx(ctx, nil)
	if err != nil {
		h.Logger.Error("%v", err)
		return httperr.Db(ctx, err)
	}

	defer func() {
		_ = tx.Rollback()
	}()

	UserProfileUpd := &models.UserProfileUpd{
		UserUid:         authToken.User.UserUid,
		Overview:        payload.Overview,
		Name:            payload.Name,
		Username:        payload.Username,
		UsernameVisible: payload.UsernameVisible,
		Email:           emailPtr,
		EmailVisible:    payload.EmailVisible,
		Phone:           payload.Phone,
		PhoneVisible:    payload.PhoneVisible,
		Password:        passwordPtr,
	}

	if err := h.Store.Users.Update_UserCoreByUid(ctx, tx, UserProfileUpd); err != nil {
		h.Logger.Error("%v", err)
		return httperr.Db(ctx, err)
	}

	if err := h.Store.Users.Update_UserProfileByUid(ctx, tx, UserProfileUpd); err != nil {
		h.Logger.Error("%v", err)
		return httperr.Db(ctx, err)
	}

	if err := h.Store.Users.Update_UserProfileAccessByUid(ctx, tx, UserProfileUpd); err != nil {
		h.Logger.Error("%v", err)
		return httperr.Db(ctx, err)
	}

	if err := tx.Commit(); err != nil {
		h.Logger.Error("%v", err)
		return httperr.Db(ctx, err)
	}

	httpx.HttpResponse(w, r, http.StatusOK, "Профиль обновлен")
	return nil
}

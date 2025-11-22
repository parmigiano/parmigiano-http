package auth

import (
	"fmt"
	"math/rand"
	"parmigiano/http/infra/constants"
	"parmigiano/http/infra/store/redis"
	"parmigiano/http/pkg"
	"parmigiano/http/pkg/security"
	"parmigiano/http/types"
	"regexp"
	"strings"

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

	// clear spaces
	payload.Name = strings.TrimSpace(payload.Name)
	payload.Username = strings.TrimSpace(payload.Username)

	valid := regexp.MustCompile(`^[a-zA-Z0-9_]+$`)
	if !valid.MatchString(payload.Name) {
		return httperr.BadRequest("недопустимые символы в имени")
	}

	if !valid.MatchString(payload.Username) {
		return httperr.BadRequest("недопустимые символы в имени пользователя")
	}

	password := strings.ToLower(strings.TrimSpace(payload.Password))
	email := strings.ToLower(strings.ReplaceAll(strings.TrimSpace(payload.Email), " ", ""))

	if _, chPass := constants.CheckSimplePasswords[password]; chPass {
		return httperr.BadRequest("пароль слишком простой, введите новый")
	}

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

	pass, err := security.HashPassword(password)
	if err != nil {
		h.Logger.Error("%v", err)
		return httperr.InternalServerError("ошибка при создании пользователя")
	}

	UserCore := &models.UserCore{
		UserUid:  uint64(uid),
		Email:    email,
		Password: pass,
	}

	if errUserCore := h.Store.Users.Create_UserCore(tx, ctx, UserCore); errUserCore != nil {
		h.Logger.Error("%v", errUserCore)
		return httperr.Db(ctx, errUserCore)
	}

	UserProfileModel := &models.UserProfile{
		UserUid:  uint64(uid),
		Avatar:   nil,
		Name:     payload.Name,
		Username: payload.Username,
	}

	if err := h.Store.Users.Create_UserProfile(tx, ctx, UserProfileModel); err != nil {
		h.Logger.Error("%v", err)
		return httperr.Db(ctx, err)
	}

	UserProfileAccessModel := &models.UserProfileAccess{
		UserUid:         uint64(uid),
		UsernameVisible: true,
		EmailVisible:    true,
		PhoneVisible:    false,
	}

	if err := h.Store.Users.Create_UserProfileAccess(tx, ctx, UserProfileAccessModel); err != nil {
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

	// send link to email for confirm
	// ------------------------------
	link := util.GenerateVerificationEmailLink(UserActiveModel.UserUid)

	go func() {
		if errSendEmail := pkg.SendEmail(UserCore.Email, "Подтверждения адреса электронной почты ParmigianoChat", fmt.Sprintf(`
			<body>
				<p>Мы получили запрос на использование адреса электронной почты <b>%s</b></p>
				<p>Чтобы завершить настройку, перейдите по ссылке для подтверждения электронной почты:</p>

				<a href="%s">
					%s
				</a>

				<p>Срок действия ссылки истечет через 24 часа...</p>

				<p>P.S. Данное письмо сгенерировано и отправлено автоматически. Пожалуйста, не отвечайте на него</p>
			</body>
		`, UserCore.Email, link, link)); errSendEmail != nil {
			h.Logger.Error("%v", errSendEmail)
		}

		h.Logger.Info("Reset link for %s: %s", UserCore.Email, link)
	}()
	// ------------------------------
	// send link to email for confirm

	session := &types.Session{
		UserUid: UserCore.UserUid,
	}

	sessionId, err := redis.CreateSession(session)
	if err != nil {
		h.Logger.Error("%v", err)
		return httperr.Db(ctx, err)
	}

	httpx.HttpResponse(w, r, http.StatusCreated, sessionId)
	return nil
}

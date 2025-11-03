package auth

import (
	"fmt"
	"net/http"
	"parmigiano/http/handler/wsocket"
	"parmigiano/http/infra/constants"
	"parmigiano/http/pkg"
	"parmigiano/http/pkg/httpx"
	"parmigiano/http/types"
	"parmigiano/http/util"
	"strconv"
)

func renderHtml(title, message string) string {
	return fmt.Sprintf(`<!DOCTYPE html>
	<html lang="ru">
	<head>
	<meta charset="UTF-8">
	<link rel="icon" type="image" href="https://github.com/parmigiano/parmigiano-desktop/blob/main/Public/assets/logo-ico.png?raw=true" />
	<meta name="viewport" content="width=device-width, initial-scale=1.0">
	<title>Подтверждение почты</title>
	<style>
	  body {
      margin: 0;
      padding: 0;
      background-color: #000;
      color: #fff;
      font-family: "Segoe UI", Arial, sans-serif;
      display: flex;
      justify-content: center;
      align-items: center;
      height: 100vh;
    }

    .container {
      	flex-direction: column;
		justify-content: center;
		text-align: center;
		gap: 2rem;
		width: 90vw;
    }

	.text-block {
		max-width: 600px;
		margin: 0 auto;
	}

    h1 {
      font-size: 28px;
      font-weight: 700;
      margin-bottom: 20px;
	  margin-top: 40px;
    }

    p {
      font-size: 15px;
      line-height: 1.6;
      color: #bbb;
      margin-bottom: 15px;
    }

    a {
      color: #0078ff;
      text-decoration: none;
    }

    a:hover {
      text-decoration: underline;
    }

	.logo {
		width: 240px;
		height: auto;
	}
	</style>
	</head>
	<body>
	  	<div class="container">
			<img src="https://github.com/parmigiano/parmigiano-http/blob/main/assets/parmigianochat.png?raw=true" alt="Logo" class="logo">
			<div class="text-block">
				<h1>%s</h1>
				<p>%s</p>
			</div>
		</div>
	</body>
	</html>`, title, message)
}

func (h *Handler) AuthEmailConfirmHandler(w http.ResponseWriter, r *http.Request) error {
	ctx := r.Context()

	renderError := func(msg string) {
		html := renderHtml("Ошибка подтверждения почты", msg)
		w.Header().Set("Content-Type", "text/html; charset=utf-8")
		w.WriteHeader(http.StatusOK)
		w.Write([]byte(html))
	}

	uid := r.URL.Query().Get("uid")
	exp := r.URL.Query().Get("exp")
	sig := r.URL.Query().Get("sig")

	if !util.ValidateEmailConfirmLink(uid, exp, sig) {
		renderError("недействительная или просроченная ссылка для подтверждения")
		return nil
	}

	userUid, err := strconv.Atoi(uid)
	if err != nil {
		h.Logger.Error("%v", err)
		renderError("ошибка типов на сервере")
		return nil
	}

	if err := h.Store.Users.Update_UserEmailConfirmedByUid(ctx, uint64(userUid), true); err != nil {
		h.Logger.Error("%v", err)

		renderError("Временная ошибка сервера. Повторите попытку через несколько минут.")
		return nil
	}

	// send event 'auth_email_confirmed' for user
	go func(userUid uint64) {
		hub := wsocket.GetHub()
		hub.Broadcast(map[string]any{
			"event": constants.EVENT_AUTH_EMAIL_CONFIRMED,
			"data": map[string]any{
				"user_uid": userUid,
			},
		})
	}(uint64(userUid))

	html := renderHtml("Ваша почта успешно подтверждена!", "Теперь вы можете войти в приложение ParmigianoChat и начать общение с другими пользователями. Благодарим вас за регистрацию и доверие к нашему сервису.")
	w.Header().Set("Content-Type", "text/html; charset=utf-8")
	w.WriteHeader(http.StatusOK)
	w.Write([]byte(html))

	return nil
}

func (h *Handler) AuthEmailConfirmReqHandler(w http.ResponseWriter, r *http.Request) error {
	ctx := r.Context()
	authToken := ctx.Value("identity").(*types.AuthToken)

	// send link to email for confirm
	// ------------------------------
	link := util.GenerateVerificationEmailLink(authToken.User.UserUid)

	go func() {
		if errSendEmail := pkg.SendEmail(authToken.User.Email, "Подтверждения адреса электронной почты ParmigianoChat", fmt.Sprintf(`
			<body>
				<p>Мы получили запрос на использование адреса электронной почты <b>%s</b></p>
				<p>Чтобы завершить настройку, перейдите по ссылке для подтверждения электронной почты:</p>

				<a href="%s">
					%s
				</a>

				<p>Срок действия ссылки истечет через 30 минут...</p>
			</body>
		`, authToken.User.Email, link, link)); errSendEmail != nil {
			h.Logger.Error("%v", errSendEmail)
		}

		h.Logger.Info("Reset link for %s: %s", authToken.User.Email, link)
	}()
	// ------------------------------
	// send link to email for confirm

	httpx.HttpResponse(w, r, http.StatusAccepted, "Ссылка подтверждения отправлена на почту")
	return nil
}

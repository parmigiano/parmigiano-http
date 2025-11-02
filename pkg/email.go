package pkg

import (
	"fmt"
	"os"
	"parmigiano/http/infra/logger"
	"strconv"

	"gopkg.in/gomail.v2"
)

func SendEmail(to, title, content string) error {
	logger := logger.NewLogger()

	mail := gomail.NewMessage()
	mail.SetHeader("From", os.Getenv("SMTP_EMAIL"))
	mail.SetHeader("To", to)
	mail.SetHeader("Subject", title)
	mail.SetBody("text/html", content)

	port, _ := strconv.Atoi(os.Getenv("SMTP_PORT"))

	d := gomail.NewDialer(os.Getenv("SMTP_ADDR"), port, os.Getenv("SMTP_EMAIL"), os.Getenv("SMTP_PASSWORD"))
	d.SSL = false

	if err := d.DialAndSend(mail); err != nil {
		return fmt.Errorf("%s: %v", "ошибка отправки email письма", err)
	}

	logger.Info("email sent: %s", to)
	return nil
}

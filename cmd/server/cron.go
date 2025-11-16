package main

import (
	"context"

	"github.com/robfig/cron/v3"
)

func (s *httpServer) checkUserIfEmailNotConfirmed() {
	c := cron.New()

	_, err := c.AddFunc("0 0 * * *", func() {
		s.logger.Info("Check users when email not confirmed")

		if err := s.store.Users.Delete_UserIfEmailNotConfirmed(context.Background()); err != nil {
			s.logger.Error("%v", err)
		}
	})

	if err != nil {
		s.logger.Error("%v", err)
	}

	c.Start()
}

package util

import (
	"net/http"
	"time"

	"api-signature/model"

	"github.com/gin-gonic/gin"
)

func SuccessResponse(c *gin.Context, data interface{}) {
	c.JSON(http.StatusOK, model.ApiResponse{
		Code:    int(model.ErrCodeSuccess),
		Message: model.ErrCodeSuccess.String(),
		Data:    data,
		Time:    time.Now().Unix(),
	})
}

func ErrorResponse(c *gin.Context, httpStatus int, errorCode model.ErrorCode, errMsg ...string) {
	message := errorCode.String()
	if len(errMsg) > 0 && errMsg[0] != "" {
		message = errMsg[0]
	}
	c.JSON(httpStatus, model.ApiResponse{
		Code:    int(errorCode),
		Message: message,
		Time:    time.Now().Unix(),
	})
}

func SuccessWithMessage(c *gin.Context, message string, data interface{}) {
	c.JSON(http.StatusOK, model.ApiResponse{
		Code:    int(model.ErrCodeSuccess),
		Message: message,
		Data:    data,
		Time:    time.Now().Unix(),
	})
}

func BadRequestResponse(c *gin.Context, errorCode model.ErrorCode) {
	ErrorResponse(c, http.StatusBadRequest, errorCode)
}

func UnauthorizedResponse(c *gin.Context, errorCode model.ErrorCode) {
	ErrorResponse(c, http.StatusUnauthorized, errorCode)
}

func ForbiddenResponse(c *gin.Context, errorCode model.ErrorCode) {
	ErrorResponse(c, http.StatusForbidden, errorCode)
}

func TooManyRequestsResponse(c *gin.Context, errorCode model.ErrorCode) {
	ErrorResponse(c, http.StatusTooManyRequests, errorCode)
}

func InternalServerErrorResponse(c *gin.Context) {
	ErrorResponse(c, http.StatusInternalServerError, model.ErrCodeInternalError)
}

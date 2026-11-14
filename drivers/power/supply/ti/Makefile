bq2597x_charger_main-y := \
	ti_charger_iio.o \
	bq2597x_charger.o \
	pd_policy_manager.o \
	cp_qc30.o

bq2597x_charger_main-$(CONFIG_CHARGER_LN8000) += ln8000_charger.o

obj-$(CONFIG_BQ2597X_CHARGE_PUMP) += bq2597x_charger_main.o

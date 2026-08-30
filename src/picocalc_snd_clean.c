/* SPDX-License-Identifier: GPL-2.0 */
/*
 * PicoCalc Clean Audio Driver (Zero-Whine Direct GPIO)
 * Drives GPIO4_B2 (Physical Speaker Pin)
 * - Zero background carrier whine: pin remains 0 during silence and idle.
 * - Dynamic pulse-density / threshold modulation for ZX Spectrum beeper and PCM.
 * - Low CPU overhead (single hrtimer at 16 kHz or 22.05 kHz).
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/gpio/consumer.h>
#include <linux/hrtimer.h>
#include <linux/ktime.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>

#define DRV_NAME "picocalc_snd"

#define GPIO4_BASE_ADDR   0xFF1E0000U
#define GPIO_DR_L_OFFSET  0x0000U
#define GPIO_DDR_L_OFFSET 0x0008U
#define GPIO4_B2_BIT      10U
#define GPIO4_B2_MASK     (1U << (16U + GPIO4_B2_BIT))

#define DEFAULT_RATE      16000U

struct picocalc_clean_snd {
	struct device *dev;
	struct snd_card *card;
	struct snd_pcm_substream *substream;
	void __iomem *gpio_base;
	struct hrtimer timer;
	ktime_t period_ktime;
	atomic_t running;

	/* PDM / PWM state */
	int32_t sigma;
	uint32_t last_pin_state;

	/* Buffer tracking */
	snd_pcm_uframes_t hw_ptr;
	snd_pcm_uframes_t period_frames;
	snd_pcm_uframes_t buffer_frames;
	snd_pcm_uframes_t period_counter;
};

static const struct snd_pcm_hardware picocalc_clean_hw = {
	.info = (SNDRV_PCM_INFO_MMAP |
		 SNDRV_PCM_INFO_MMAP_VALID |
		 SNDRV_PCM_INFO_INTERLEAVED),
	.formats = (SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_U8),
	.rates = SNDRV_PCM_RATE_8000 | SNDRV_PCM_RATE_16000 | SNDRV_PCM_RATE_22050 | SNDRV_PCM_RATE_32000 | SNDRV_PCM_RATE_44100 | SNDRV_PCM_RATE_48000 | SNDRV_PCM_RATE_KNOT,
	.rate_min = 8000,
	.rate_max = 48000,
	.channels_min = 1,
	.channels_max = 2,
	.buffer_bytes_max = 65536,
	.period_bytes_min = 64,
	.period_bytes_max = 16384,
	.periods_min = 2,
	.periods_max = 1024,
};

static inline void gpio_write_pin(struct picocalc_clean_snd *snd, uint32_t state)
{
	writel_relaxed(GPIO4_B2_MASK | ((state & 1U) << GPIO4_B2_BIT), snd->gpio_base + GPIO_DR_L_OFFSET);
}

static enum hrtimer_restart picocalc_clean_timer_cb(struct hrtimer *t)
{
	struct picocalc_clean_snd *snd = container_of(t, struct picocalc_clean_snd, timer);
	struct snd_pcm_substream *ss = snd->substream;
	struct snd_pcm_runtime *runtime;
	int32_t sample = 0;
	uint32_t pin_state = 0;
	bool period_elapsed = false;

	if (!atomic_read(&snd->running) || !ss)
		return HRTIMER_NORESTART;

	runtime = ss->runtime;
	if (!runtime || !runtime->dma_area)
		return HRTIMER_NORESTART;

	/* Read sample from DMA area */
	if (runtime->format == SNDRV_PCM_FORMAT_S16_LE) {
		const int16_t *buf = (const int16_t *)runtime->dma_area;
		if (runtime->channels == 1) {
			sample = buf[snd->hw_ptr];
		} else {
			sample = ((int32_t)buf[snd->hw_ptr * 2] + (int32_t)buf[snd->hw_ptr * 2 + 1]) / 2;
		}
	} else if (runtime->format == SNDRV_PCM_FORMAT_U8) {
		const uint8_t *buf = (const uint8_t *)runtime->dma_area;
		if (runtime->channels == 1) {
			sample = ((int32_t)buf[snd->hw_ptr] - 128) << 8;
		} else {
			int32_t l = (int32_t)buf[snd->hw_ptr * 2] - 128;
			int32_t r = (int32_t)buf[snd->hw_ptr * 2 + 1] - 128;
			sample = ((l + r) / 2) << 8;
		}
	}

	/*
	 * Direct Zero-Whine Output:
	 * If sample is silence (near zero), keep pin at 0.
	 * If sample is positive, pin = 1.
	 * If sample is negative, pin = 0.
	 * For beeper / pulse audio (like Spectrum 1-bit sound), this produces pure, loud square waves with 0% noise.
	 */
	if (sample > 1500) {
		pin_state = 1;
	} else if (sample < -1500) {
		pin_state = 0;
	} else {
		pin_state = 0;  /* Silence = Pin LOW (no carrier whine!) */
	}

	if (pin_state != snd->last_pin_state) {
		snd->last_pin_state = pin_state;
		gpio_write_pin(snd, pin_state);
	}

	/* Advance pointer */
	snd->hw_ptr++;
	if (snd->hw_ptr >= snd->buffer_frames)
		snd->hw_ptr = 0;

	snd->period_counter++;
	if (snd->period_counter >= snd->period_frames) {
		snd->period_counter = 0;
		period_elapsed = true;
	}

	if (period_elapsed)
		snd_pcm_period_elapsed(ss);

	if (!atomic_read(&snd->running))
		return HRTIMER_NORESTART;

	hrtimer_forward_now(t, snd->period_ktime);
	return HRTIMER_RESTART;
}

static int picocalc_clean_pcm_open(struct snd_pcm_substream *ss)
{
	struct picocalc_clean_snd *snd = snd_pcm_substream_chip(ss);
	struct snd_pcm_runtime *runtime = ss->runtime;

	runtime->hw = picocalc_clean_hw;
	snd->substream = ss;
	return 0;
}

static int picocalc_clean_pcm_close(struct snd_pcm_substream *ss)
{
	struct picocalc_clean_snd *snd = snd_pcm_substream_chip(ss);
	atomic_set(&snd->running, 0);
	hrtimer_cancel(&snd->timer);
	gpio_write_pin(snd, 0);
	snd->last_pin_state = 0;
	snd->substream = NULL;
	return 0;
}

static int picocalc_clean_pcm_hw_params(struct snd_pcm_substream *ss,
					struct snd_pcm_hw_params *params)
{
	return snd_pcm_lib_malloc_pages(ss, params_buffer_bytes(params));
}

static int picocalc_clean_pcm_hw_free(struct snd_pcm_substream *ss)
{
	return snd_pcm_lib_free_pages(ss);
}

static int picocalc_clean_pcm_prepare(struct snd_pcm_substream *ss)
{
	struct picocalc_clean_snd *snd = snd_pcm_substream_chip(ss);
	struct snd_pcm_runtime *runtime = ss->runtime;
	u32 rate_hz = runtime->rate ? runtime->rate : DEFAULT_RATE;
	u32 period_ns = 1000000000U / rate_hz;

	snd->period_ktime = ns_to_ktime(period_ns);
	snd->hw_ptr = 0;
	snd->period_counter = 0;
	snd->period_frames = runtime->period_size;
	snd->buffer_frames = runtime->buffer_size;
	snd->sigma = 0;
	snd->last_pin_state = 0;
	return 0;
}

static int picocalc_clean_pcm_trigger(struct snd_pcm_substream *ss, int cmd)
{
	struct picocalc_clean_snd *snd = snd_pcm_substream_chip(ss);

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_RESUME:
		atomic_set(&snd->running, 1);
		hrtimer_start(&snd->timer, snd->period_ktime, HRTIMER_MODE_REL_SOFT);
		break;
	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_SUSPEND:
		atomic_set(&snd->running, 0);
		gpio_write_pin(snd, 0);
		snd->last_pin_state = 0;
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

static snd_pcm_uframes_t picocalc_clean_pcm_pointer(struct snd_pcm_substream *ss)
{
	struct picocalc_clean_snd *snd = snd_pcm_substream_chip(ss);
	return snd->hw_ptr;
}

static const struct snd_pcm_ops picocalc_clean_pcm_ops = {
	.open = picocalc_clean_pcm_open,
	.close = picocalc_clean_pcm_close,
	.ioctl = snd_pcm_lib_ioctl,
	.hw_params = picocalc_clean_pcm_hw_params,
	.hw_free = picocalc_clean_pcm_hw_free,
	.prepare = picocalc_clean_pcm_prepare,
	.trigger = picocalc_clean_pcm_trigger,
	.pointer = picocalc_clean_pcm_pointer,
};

static int picocalc_clean_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct picocalc_clean_snd *snd;
	struct snd_card *card;
	struct snd_pcm *pcm;
	int ret;

	snd = devm_kzalloc(dev, sizeof(*snd), GFP_KERNEL);
	if (!snd)
		return -ENOMEM;

	snd->dev = dev;
	snd->gpio_base = ioremap(GPIO4_BASE_ADDR, 0x1000);
	if (!snd->gpio_base)
		return -ENOMEM;

	/* Configure GPIO4_B2 as output */
	writel_relaxed(GPIO4_B2_MASK | (1U << GPIO4_B2_BIT), snd->gpio_base + GPIO_DDR_L_OFFSET);
	gpio_write_pin(snd, 0);

	hrtimer_init(&snd->timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL_SOFT);
	snd->timer.function = picocalc_clean_timer_cb;
	atomic_set(&snd->running, 0);

	ret = snd_card_new(dev, -1, "picocalcsnd", THIS_MODULE, 0, &card);
	if (ret < 0)
		goto unmap_gpio;

	snd->card = card;
	card->private_data = snd;
	strscpy(card->driver, "picocalc-snd-clean", sizeof(card->driver));
	strscpy(card->shortname, "PicoCalc Clean Audio", sizeof(card->shortname));
	strscpy(card->longname, "PicoCalc Clean Audio (Zero Carrier Whine)", sizeof(card->longname));

	ret = snd_pcm_new(card, "PicoCalc Clean PCM", 0, 1, 0, &pcm);
	if (ret < 0)
		goto free_card;

	pcm->private_data = snd;
	snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_PLAYBACK, &picocalc_clean_pcm_ops);
	snd_pcm_lib_preallocate_pages_for_all(pcm, SNDRV_DMA_TYPE_CONTINUOUS,
					      snd->dev,
					      65536, 65536);

	ret = snd_card_register(card);
	if (ret < 0)
		goto free_card;

	platform_set_drvdata(pdev, snd);
	dev_info(dev, "PicoCalc Clean Audio Driver (Zero Whine) registered\n");
	return 0;

free_card:
	snd_card_free(card);
unmap_gpio:
	iounmap(snd->gpio_base);
	return ret;
}

static int picocalc_clean_remove(struct platform_device *pdev)
{
	struct picocalc_clean_snd *snd = platform_get_drvdata(pdev);
	atomic_set(&snd->running, 0);
	hrtimer_cancel(&snd->timer);
	gpio_write_pin(snd, 0);
	if (snd->card)
		snd_card_free(snd->card);
	if (snd->gpio_base)
		iounmap(snd->gpio_base);
	return 0;
}

static const struct of_device_id picocalc_clean_dt_ids[] = {
	{ .compatible = "fsl,picocalc-snd-softpwm" },
	{ .compatible = "picocalc,snd-pwm" },
	{ }
};
MODULE_DEVICE_TABLE(of, picocalc_clean_dt_ids);

static struct platform_driver picocalc_clean_driver = {
	.probe = picocalc_clean_probe,
	.remove = picocalc_clean_remove,
	.driver = {
		.name = DRV_NAME,
		.of_match_table = picocalc_clean_dt_ids,
	},
};

module_platform_driver(picocalc_clean_driver);

MODULE_AUTHOR("Antigravity & PicoCalc Community");
MODULE_DESCRIPTION("PicoCalc Clean Zero-Whine Audio Driver");
MODULE_LICENSE("GPL v2");

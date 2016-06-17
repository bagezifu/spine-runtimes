/******************************************************************************
 * Spine Runtimes Software License
 * Version 2.3
 * 
 * Copyright (c) 2013-2015, Esoteric Software
 * All rights reserved.
 * 
 * You are granted a perpetual, non-exclusive, non-sublicensable and
 * non-transferable license to use, install, execute and perform the Spine
 * Runtimes Software (the "Software") and derivative works solely for personal
 * or internal use. Without the written permission of Esoteric Software (see
 * Section 2 of the Spine Software License Agreement), you may not (a) modify,
 * translate, adapt or otherwise create derivative works, improvements of the
 * Software or develop new applications using the Software or (b) remove,
 * delete, alter or obscure any trademarks or any copyright, trademark, patent
 * or other intellectual property or proprietary rights notices on or in the
 * Software, including any copy thereof. Redistributions in binary or source
 * form must include this license and terms.
 * 
 * THIS SOFTWARE IS PROVIDED BY ESOTERIC SOFTWARE "AS IS" AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO
 * EVENT SHALL ESOTERIC SOFTWARE BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *****************************************************************************/

#include <spine/Animation.h>
#include <spine/IkConstraint.h>
#include <limits.h>
#include <spine/extension.h>

spAnimation* spAnimation_create (const char* name, int timelinesCount) {
	spAnimation* self = NEW(spAnimation);
	MALLOC_STR(self->name, name);
	self->timelinesCount = timelinesCount;
	self->timelines = MALLOC(spTimeline*, timelinesCount);
	return self;
}

void spAnimation_dispose (spAnimation* self) {
	int i;
	for (i = 0; i < self->timelinesCount; ++i)
		spTimeline_dispose(self->timelines[i]);
	FREE(self->timelines);
	FREE(self->name);
	FREE(self);
}

void spAnimation_apply (const spAnimation* self, spSkeleton* skeleton, float lastTime, float time, int loop, spEvent** events,
		int* eventsCount) {
	int i, n = self->timelinesCount;

	if (loop && self->duration) {
		time = FMOD(time, self->duration);
		if (lastTime > 0) lastTime = FMOD(lastTime, self->duration);
	}

	for (i = 0; i < n; ++i)
		spTimeline_apply(self->timelines[i], skeleton, lastTime, time, events, eventsCount, 1);
}

void spAnimation_mix (const spAnimation* self, spSkeleton* skeleton, float lastTime, float time, int loop, spEvent** events,
		int* eventsCount, float alpha) {
	int i, n = self->timelinesCount;

	if (loop && self->duration) {
		time = FMOD(time, self->duration);
		if (lastTime > 0) lastTime = FMOD(lastTime, self->duration);
	}

	for (i = 0; i < n; ++i)
		spTimeline_apply(self->timelines[i], skeleton, lastTime, time, events, eventsCount, alpha);
}

/**/

typedef struct _spTimelineVtable {
	void (*apply) (const spTimeline* self, spSkeleton* skeleton, float lastTime, float time, spEvent** firedEvents,
			int* eventsCount, float alpha);
	void (*dispose) (spTimeline* self);
} _spTimelineVtable;

void _spTimeline_init (spTimeline* self, spTimelineType type, /**/
void (*dispose) (spTimeline* self), /**/
		void (*apply) (const spTimeline* self, spSkeleton* skeleton, float lastTime, float time, spEvent** firedEvents,
				int* eventsCount, float alpha)) {
	CONST_CAST(spTimelineType, self->type) = type;
	CONST_CAST(_spTimelineVtable*, self->vtable) = NEW(_spTimelineVtable);
	VTABLE(spTimeline, self)->dispose = dispose;
	VTABLE(spTimeline, self)->apply = apply;
}

void _spTimeline_deinit (spTimeline* self) {
	FREE(self->vtable);
}

void spTimeline_dispose (spTimeline* self) {
	VTABLE(spTimeline, self)->dispose(self);
}

void spTimeline_apply (const spTimeline* self, spSkeleton* skeleton, float lastTime, float time, spEvent** firedEvents,
		int* eventsCount, float alpha) {
	VTABLE(spTimeline, self)->apply(self, skeleton, lastTime, time, firedEvents, eventsCount, alpha);
}

/**/

static const float CURVE_LINEAR = 0, CURVE_STEPPED = 1, CURVE_BEZIER = 2;
static const int BEZIER_SEGMENTS = 10, BEZIER_SIZE = 10 * 2 - 1;

void _spCurveTimeline_init (spCurveTimeline* self, spTimelineType type, int framesCount, /**/
void (*dispose) (spTimeline* self), /**/
		void (*apply) (const spTimeline* self, spSkeleton* skeleton, float lastTime, float time, spEvent** firedEvents,
				int* eventsCount, float alpha)) {
	_spTimeline_init(SUPER(self), type, dispose, apply);
	self->curves = CALLOC(float, (framesCount - 1) * BEZIER_SIZE);
}

void _spCurveTimeline_deinit (spCurveTimeline* self) {
	_spTimeline_deinit(SUPER(self));
	FREE(self->curves);
}

void spCurveTimeline_setLinear (spCurveTimeline* self, int frameIndex) {
	self->curves[frameIndex * BEZIER_SIZE] = CURVE_LINEAR;
}

void spCurveTimeline_setStepped (spCurveTimeline* self, int frameIndex) {
	self->curves[frameIndex * BEZIER_SIZE] = CURVE_STEPPED;
}

void spCurveTimeline_setCurve (spCurveTimeline* self, int frameIndex, float cx1, float cy1, float cx2, float cy2) {
	float subdiv1 = 1.0f / BEZIER_SEGMENTS, subdiv2 = subdiv1 * subdiv1, subdiv3 = subdiv2 * subdiv1;
	float pre1 = 3 * subdiv1, pre2 = 3 * subdiv2, pre4 = 6 * subdiv2, pre5 = 6 * subdiv3;
	float tmp1x = -cx1 * 2 + cx2, tmp1y = -cy1 * 2 + cy2, tmp2x = (cx1 - cx2) * 3 + 1, tmp2y = (cy1 - cy2) * 3 + 1;
	float dfx = cx1 * pre1 + tmp1x * pre2 + tmp2x * subdiv3, dfy = cy1 * pre1 + tmp1y * pre2 + tmp2y * subdiv3;
	float ddfx = tmp1x * pre4 + tmp2x * pre5, ddfy = tmp1y * pre4 + tmp2y * pre5;
	float dddfx = tmp2x * pre5, dddfy = tmp2y * pre5;
	float x = dfx, y = dfy;

	int i = frameIndex * BEZIER_SIZE, n = i + BEZIER_SIZE - 1;
	self->curves[i++] = CURVE_BEZIER;

	for (; i < n; i += 2) {
		self->curves[i] = x;
		self->curves[i + 1] = y;
		dfx += ddfx;
		dfy += ddfy;
		ddfx += dddfx;
		ddfy += dddfy;
		x += dfx;
		y += dfy;
	}
}

float spCurveTimeline_getCurvePercent (const spCurveTimeline* self, int frameIndex, float percent) {
	float x, y;
	int i = frameIndex * BEZIER_SIZE, start, n;
	float type = self->curves[i];
	if (type == CURVE_LINEAR) return percent;
	if (type == CURVE_STEPPED) return 0;
	i++;
	x = 0;
	for (start = i, n = i + BEZIER_SIZE - 1; i < n; i += 2) {
		x = self->curves[i];
		if (x >= percent) {
			float prevX, prevY;
			if (i == start) {
				prevX = 0;
				prevY = 0;
			} else {
				prevX = self->curves[i - 2];
				prevY = self->curves[i - 1];
			}
			return prevY + (self->curves[i + 1] - prevY) * (percent - prevX) / (x - prevX);
		}
	}
	y = self->curves[i - 1];
	return y + (1 - y) * (percent - x) / (1 - x); /* Last point is 1,1. */
}

/* @param target After the first and before the last entry. */
static int binarySearch (float *values, int valuesLength, float target, int step) {
	int low = 0, current;
	int high = valuesLength / step - 2;
	if (high == 0) return step;
	current = high >> 1;
	while (1) {
		if (values[(current + 1) * step] <= target)
			low = current + 1;
		else
			high = current;
		if (low == high) return (low + 1) * step;
		current = (low + high) >> 1;
	}
	return 0;
}

/* @param target After the first and before the last entry. */
static int binarySearch1 (float *values, int valuesLength, float target) {
	int low = 0, current;
	int high = valuesLength - 2;
	if (high == 0) return 1;
	current = high >> 1;
	while (1) {
		if (values[(current + 1)] <= target)
			low = current + 1;
		else
			high = current;
		if (low == high) return low + 1;
		current = (low + high) >> 1;
	}
	return 0;
}

/*static int linearSearch (float *values, int valuesLength, float target, int step) {
 int i, last = valuesLength - step;
 for (i = 0; i <= last; i += step) {
 if (values[i] <= target) continue;
 return i;
 }
 return -1;
 }*/

/**/

void _spBaseTimeline_dispose (spTimeline* timeline) {
	struct spBaseTimeline* self = SUB_CAST(struct spBaseTimeline, timeline);
	_spCurveTimeline_deinit(SUPER(self));
	FREE(self->frames);
	FREE(self);
}

/* Many timelines have structure identical to struct spBaseTimeline and extend spCurveTimeline. **/
struct spBaseTimeline* _spBaseTimeline_create (int framesCount, spTimelineType type, int frameSize, /**/
		void (*apply) (const spTimeline* self, spSkeleton* skeleton, float lastTime, float time, spEvent** firedEvents,
				int* eventsCount, float alpha)) {
	struct spBaseTimeline* self = NEW(struct spBaseTimeline);
	_spCurveTimeline_init(SUPER(self), type, framesCount, _spBaseTimeline_dispose, apply);

	CONST_CAST(int, self->framesCount) = framesCount * frameSize;
	CONST_CAST(float*, self->frames) = CALLOC(float, self->framesCount);

	return self;
}

/**/

static const int ROTATE_PREV_TIME = -2;
static const int ROTATE_FRAME = 1;

void _spRotateTimeline_apply (const spTimeline* timeline, spSkeleton* skeleton, float lastTime, float time, spEvent** firedEvents,
		int* eventsCount, float alpha) {
	spBone *bone;
	int frame;
	float prevFrameValue, frameTime, percent, amount;

	spRotateTimeline* self = SUB_CAST(spRotateTimeline, timeline);

	if (time < self->frames[0]) return; /* Time is before first frame. */

	bone = skeleton->bones[self->boneIndex];

	if (time >= self->frames[self->framesCount - 2]) { /* Time is after last frame. */
		amount = bone->data->rotation + self->frames[self->framesCount - 1] - bone->rotation;
		while (amount > 180)
			amount -= 360;
		while (amount < -180)
			amount += 360;
		bone->rotation += amount * alpha;
		return;
	}

	/* Interpolate between the previous frame and the current frame. */
	frame = binarySearch(self->frames, self->framesCount, time, 2);
	prevFrameValue = self->frames[frame - 1];
	frameTime = self->frames[frame];
	percent = 1 - (time - frameTime) / (self->frames[frame + ROTATE_PREV_TIME] - frameTime);
	percent = spCurveTimeline_getCurvePercent(SUPER(self), (frame >> 1) - 1, percent < 0 ? 0 : (percent > 1 ? 1 : percent));

	amount = self->frames[frame + ROTATE_FRAME] - prevFrameValue;
	while (amount > 180)
		amount -= 360;
	while (amount < -180)
		amount += 360;
	amount = bone->data->rotation + (prevFrameValue + amount * percent) - bone->rotation;
	while (amount > 180)
		amount -= 360;
	while (amount < -180)
		amount += 360;
	bone->rotation += amount * alpha;

	UNUSED(lastTime);
	UNUSED(firedEvents);
	UNUSED(eventsCount);
}

spRotateTimeline* spRotateTimeline_create (int framesCount) {
	return _spBaseTimeline_create(framesCount, SP_TIMELINE_ROTATE, 2, _spRotateTimeline_apply);
}

void spRotateTimeline_setFrame (spRotateTimeline* self, int frameIndex, float time, float angle) {
	frameIndex *= 2;
	self->frames[frameIndex] = time;
	self->frames[frameIndex + 1] = angle;
}

/**/

static const int TRANSLATE_PREV_TIME = -3;
static const int TRANSLATE_X = 1;
static const int TRANSLATE_Y = 2;

void _spTranslateTimeline_apply (const spTimeline* timeline, spSkeleton* skeleton, float lastTime, float time,
		spEvent** firedEvents, int* eventsCount, float alpha) {
	spBone *bone;
	int frame;
	float prevFrameX, prevFrameY, frameTime, percent;

	spTranslateTimeline* self = SUB_CAST(spTranslateTimeline, timeline);

	if (time < self->frames[0]) return; /* Time is before first frame. */

	bone = skeleton->bones[self->boneIndex];

	if (time >= self->frames[self->framesCount - 3]) { /* Time is after last frame. */
		bone->x += (bone->data->x + self->frames[self->framesCount - 2] - bone->x) * alpha;
		bone->y += (bone->data->y + self->frames[self->framesCount - 1] - bone->y) * alpha;
		return;
	}

	/* Interpolate between the previous frame and the current frame. */
	frame = binarySearch(self->frames, self->framesCount, time, 3);
	prevFrameX = self->frames[frame - 2];
	prevFrameY = self->frames[frame - 1];
	frameTime = self->frames[frame];
	percent = 1 - (time - frameTime) / (self->frames[frame + TRANSLATE_PREV_TIME] - frameTime);
	percent = spCurveTimeline_getCurvePercent(SUPER(self), frame / 3 - 1, percent < 0 ? 0 : (percent > 1 ? 1 : percent));

	bone->x += (bone->data->x + prevFrameX + (self->frames[frame + TRANSLATE_X] - prevFrameX) * percent - bone->x)
			* alpha;
	bone->y += (bone->data->y + prevFrameY + (self->frames[frame + TRANSLATE_Y] - prevFrameY) * percent - bone->y)
			* alpha;

	UNUSED(lastTime);
	UNUSED(firedEvents);
	UNUSED(eventsCount);
}

spTranslateTimeline* spTranslateTimeline_create (int framesCount) {
	return _spBaseTimeline_create(framesCount, SP_TIMELINE_TRANSLATE, 3, _spTranslateTimeline_apply);
}

void spTranslateTimeline_setFrame (spTranslateTimeline* self, int frameIndex, float time, float x, float y) {
	frameIndex *= 3;
	self->frames[frameIndex] = time;
	self->frames[frameIndex + 1] = x;
	self->frames[frameIndex + 2] = y;
}

/**/

void _spScaleTimeline_apply (const spTimeline* timeline, spSkeleton* skeleton, float lastTime, float time, spEvent** firedEvents,
		int* eventsCount, float alpha) {
	spBone *bone;
	int frame;
	float prevFrameX, prevFrameY, frameTime, percent;

	spScaleTimeline* self = SUB_CAST(spScaleTimeline, timeline);

	if (time < self->frames[0]) return; /* Time is before first frame. */

	bone = skeleton->bones[self->boneIndex];
	if (time >= self->frames[self->framesCount - 3]) { /* Time is after last frame. */
		bone->scaleX += (bone->data->scaleX * self->frames[self->framesCount - 2] - bone->scaleX) * alpha;
		bone->scaleY += (bone->data->scaleY * self->frames[self->framesCount - 1] - bone->scaleY) * alpha;
		return;
	}

	/* Interpolate between the previous frame and the current frame. */
	frame = binarySearch(self->frames, self->framesCount, time, 3);
	prevFrameX = self->frames[frame - 2];
	prevFrameY = self->frames[frame - 1];
	frameTime = self->frames[frame];
	percent = 1 - (time - frameTime) / (self->frames[frame + TRANSLATE_PREV_TIME] - frameTime);
	percent = spCurveTimeline_getCurvePercent(SUPER(self), frame / 3 - 1, percent < 0 ? 0 : (percent > 1 ? 1 : percent));

	bone->scaleX += (bone->data->scaleX * (prevFrameX + (self->frames[frame + TRANSLATE_X] - prevFrameX) * percent)
			- bone->scaleX) * alpha;
	bone->scaleY += (bone->data->scaleY * (prevFrameY + (self->frames[frame + TRANSLATE_Y] - prevFrameY) * percent)
			- bone->scaleY) * alpha;

	UNUSED(lastTime);
	UNUSED(firedEvents);
	UNUSED(eventsCount);
}

spScaleTimeline* spScaleTimeline_create (int framesCount) {
	return _spBaseTimeline_create(framesCount, SP_TIMELINE_SCALE, 3, _spScaleTimeline_apply);
}

void spScaleTimeline_setFrame (spScaleTimeline* self, int frameIndex, float time, float x, float y) {
	spTranslateTimeline_setFrame(self, frameIndex, time, x, y);
}

/**/

void _spShearTimeline_apply (const spTimeline* timeline, spSkeleton* skeleton, float lastTime, float time, spEvent** firedEvents,
							 int* eventsCount, float alpha) {
	spBone *bone;
	int frame;
	float prevFrameX, prevFrameY, frameTime, percent;

	spShearTimeline* self = SUB_CAST(spShearTimeline, timeline);

	if (time < self->frames[0]) return; /* Time is before first frame. */

	bone = skeleton->bones[self->boneIndex];
	if (time >= self->frames[self->framesCount - 3]) { /* Time is after last frame. */
		bone->shearX += (bone->data->shearX * self->frames[self->framesCount - 2] - bone->shearX) * alpha;
		bone->shearY += (bone->data->shearY * self->frames[self->framesCount - 1] - bone->shearY) * alpha;
		return;
	}

	/* Interpolate between the previous frame and the current frame. */
	frame = binarySearch(self->frames, self->framesCount, time, 3);
	prevFrameX = self->frames[frame - 2];
	prevFrameY = self->frames[frame - 1];
	frameTime = self->frames[frame];
	percent = 1 - (time - frameTime) / (self->frames[frame + TRANSLATE_PREV_TIME] - frameTime);
	percent = spCurveTimeline_getCurvePercent(SUPER(self), frame / 3 - 1, percent < 0 ? 0 : (percent > 1 ? 1 : percent));

	bone->shearX += (bone->data->shearX + (prevFrameX + (self->frames[frame + TRANSLATE_X] - prevFrameX) * percent)
					 - bone->shearX) * alpha;
	bone->shearY += (bone->data->shearY + (prevFrameY + (self->frames[frame + TRANSLATE_Y] - prevFrameY) * percent)
					 - bone->shearY) * alpha;

	UNUSED(lastTime);
	UNUSED(firedEvents);
	UNUSED(eventsCount);
}

spShearTimeline* spShearTimeline_create (int framesCount) {
	return _spBaseTimeline_create(framesCount, SP_TIMELINE_SHEAR, 3, _spShearTimeline_apply);
}

void spShearTimeline_setFrame (spShearTimeline* self, int frameIndex, float time, float x, float y) {
	spTranslateTimeline_setFrame(self, frameIndex, time, x, y);
}

/**/

static const int COLOR_PREV_TIME = -5;
static const int COLOR_R = 1;
static const int COLOR_G = 2;
static const int COLOR_B = 3;
static const int COLOR_A = 4;

void _spColorTimeline_apply (const spTimeline* timeline, spSkeleton* skeleton, float lastTime, float time, spEvent** firedEvents,
		int* eventsCount, float alpha) {
	spSlot *slot;
	int frame;
	float prevFrameR, prevFrameG, prevFrameB, prevFrameA, percent, frameTime;
	float r, g, b, a;
	spColorTimeline* self = (spColorTimeline*)timeline;

	if (time < self->frames[0]) return; /* Time is before first frame. */

	if (time >= self->frames[self->framesCount - 5]) { /* Time is after last frame */
		int i = self->framesCount - 1;
		r = self->frames[i - 3];
		g = self->frames[i - 2];
		b = self->frames[i - 1];
		a = self->frames[i];
	} else {
		/* Interpolate between the previous frame and the current frame. */
		frame = binarySearch(self->frames, self->framesCount, time, 5);
		frameTime = self->frames[frame];
		percent = 1 - (time - frameTime) / (self->frames[frame + COLOR_PREV_TIME] - frameTime);
		percent = spCurveTimeline_getCurvePercent(SUPER(self), frame / 5 - 1, percent < 0 ? 0 : (percent > 1 ? 1 : percent));

		r = self->frames[frame - 4];
		g = self->frames[frame - 3];
		b = self->frames[frame - 2];
		a = self->frames[frame - 1];

		r += (self->frames[frame + COLOR_R] - prevFrameR) * percent;
		g += (self->frames[frame + COLOR_G] - prevFrameG) * percent;
		b += (self->frames[frame + COLOR_B] - prevFrameB) * percent;
		a += (self->frames[frame + COLOR_A] - prevFrameA) * percent;
	}
	slot = skeleton->slots[self->slotIndex];
	if (alpha < 1) {
		slot->r += (r - slot->r) * alpha;
		slot->g += (g - slot->g) * alpha;
		slot->b += (b - slot->b) * alpha;
		slot->a += (a - slot->a) * alpha;
	} else {
		slot->r = r;
		slot->g = g;
		slot->b = b;
		slot->a = a;
	}

	UNUSED(lastTime);
	UNUSED(firedEvents);
	UNUSED(eventsCount);
}

spColorTimeline* spColorTimeline_create (int framesCount) {
	return (spColorTimeline*)_spBaseTimeline_create(framesCount, SP_TIMELINE_COLOR, 5, _spColorTimeline_apply);
}

void spColorTimeline_setFrame (spColorTimeline* self, int frameIndex, float time, float r, float g, float b, float a) {
	frameIndex *= 5;
	self->frames[frameIndex] = time;
	self->frames[frameIndex + 1] = r;
	self->frames[frameIndex + 2] = g;
	self->frames[frameIndex + 3] = b;
	self->frames[frameIndex + 4] = a;
}

/**/

void _spAttachmentTimeline_apply (const spTimeline* timeline, spSkeleton* skeleton, float lastTime, float time,
		spEvent** firedEvents, int* eventsCount, float alpha) {
	int frame;
	const char* attachmentName;
	spAttachmentTimeline* self = (spAttachmentTimeline*)timeline;

	if (time < self->frames[0]) {
		if (lastTime > time) _spAttachmentTimeline_apply(timeline, skeleton, lastTime, (float)INT_MAX, 0, 0, 0);
		return;
	} else if (lastTime > time) /**/
		lastTime = -1;

	frame = time >= self->frames[self->framesCount - 1] ?
		self->framesCount - 1 : binarySearch1(self->frames, self->framesCount, time) - 1;
	if (self->frames[frame] < lastTime) return;

	attachmentName = self->attachmentNames[frame];
	spSlot_setAttachment(skeleton->slots[self->slotIndex],
			attachmentName ? spSkeleton_getAttachmentForSlotIndex(skeleton, self->slotIndex, attachmentName) : 0);

	UNUSED(firedEvents);
	UNUSED(eventsCount);
	UNUSED(alpha);
}

void _spAttachmentTimeline_dispose (spTimeline* timeline) {
	spAttachmentTimeline* self = SUB_CAST(spAttachmentTimeline, timeline);
	int i;

	_spTimeline_deinit(timeline);

	for (i = 0; i < self->framesCount; ++i)
		FREE(self->attachmentNames[i]);
	FREE(self->attachmentNames);
	FREE(self->frames);
	FREE(self);
}

spAttachmentTimeline* spAttachmentTimeline_create (int framesCount) {
	spAttachmentTimeline* self = NEW(spAttachmentTimeline);
	_spTimeline_init(SUPER(self), SP_TIMELINE_ATTACHMENT, _spAttachmentTimeline_dispose, _spAttachmentTimeline_apply);

	CONST_CAST(int, self->framesCount) = framesCount;
	CONST_CAST(float*, self->frames) = CALLOC(float, framesCount);
	CONST_CAST(char**, self->attachmentNames) = CALLOC(char*, framesCount);

	return self;
}

void spAttachmentTimeline_setFrame (spAttachmentTimeline* self, int frameIndex, float time, const char* attachmentName) {
	self->frames[frameIndex] = time;

	FREE(self->attachmentNames[frameIndex]);
	if (attachmentName)
		MALLOC_STR(self->attachmentNames[frameIndex], attachmentName);
	else
		self->attachmentNames[frameIndex] = 0;
}

/**/

/** Fires events for frames > lastTime and <= time. */
void _spEventTimeline_apply (const spTimeline* timeline, spSkeleton* skeleton, float lastTime, float time, spEvent** firedEvents,
		int* eventsCount, float alpha) {
	spEventTimeline* self = (spEventTimeline*)timeline;
	int frame;
	if (!firedEvents) return;

	if (lastTime > time) { /* Fire events after last time for looped animations. */
		_spEventTimeline_apply(timeline, skeleton, lastTime, (float)INT_MAX, firedEvents, eventsCount, alpha);
		lastTime = -1;
	} else if (lastTime >= self->frames[self->framesCount - 1]) /* Last time is after last frame. */
	return;
	if (time < self->frames[0]) return; /* Time is before first frame. */

	if (lastTime < self->frames[0])
		frame = 0;
	else {
		float frameTime;
		frame = binarySearch1(self->frames, self->framesCount, lastTime);
		frameTime = self->frames[frame];
		while (frame > 0) { /* Fire multiple events with the same frame. */
			if (self->frames[frame - 1] != frameTime) break;
			frame--;
		}
	}
	for (; frame < self->framesCount && time >= self->frames[frame]; ++frame) {
		firedEvents[*eventsCount] = self->events[frame];
		(*eventsCount)++;
	}
}

void _spEventTimeline_dispose (spTimeline* timeline) {
	spEventTimeline* self = SUB_CAST(spEventTimeline, timeline);
	int i;

	_spTimeline_deinit(timeline);

	for (i = 0; i < self->framesCount; ++i)
		spEvent_dispose(self->events[i]);
	FREE(self->events);
	FREE(self->frames);
	FREE(self);
}

spEventTimeline* spEventTimeline_create (int framesCount) {
	spEventTimeline* self = NEW(spEventTimeline);
	_spTimeline_init(SUPER(self), SP_TIMELINE_EVENT, _spEventTimeline_dispose, _spEventTimeline_apply);

	CONST_CAST(int, self->framesCount) = framesCount;
	CONST_CAST(float*, self->frames) = CALLOC(float, framesCount);
	CONST_CAST(spEvent**, self->events) = CALLOC(spEvent*, framesCount);

	return self;
}

void spEventTimeline_setFrame (spEventTimeline* self, int frameIndex, spEvent* event) {
	self->frames[frameIndex] = event->time;

	FREE(self->events[frameIndex]);
	self->events[frameIndex] = event;
}

/**/

void _spDrawOrderTimeline_apply (const spTimeline* timeline, spSkeleton* skeleton, float lastTime, float time,
		spEvent** firedEvents, int* eventsCount, float alpha) {
	int i;
	int frame;
	const int* drawOrderToSetupIndex;
	spDrawOrderTimeline* self = (spDrawOrderTimeline*)timeline;

	if (time < self->frames[0]) return; /* Time is before first frame. */

	if (time >= self->frames[self->framesCount - 1]) /* Time is after last frame. */
		frame = self->framesCount - 1;
	else
		frame = binarySearch1(self->frames, self->framesCount, time) - 1;

	drawOrderToSetupIndex = self->drawOrders[frame];
	if (!drawOrderToSetupIndex)
		memcpy(skeleton->drawOrder, skeleton->slots, self->slotsCount * sizeof(spSlot*));
	else {
		for (i = 0; i < self->slotsCount; ++i)
			skeleton->drawOrder[i] = skeleton->slots[drawOrderToSetupIndex[i]];
	}

	UNUSED(lastTime);
	UNUSED(firedEvents);
	UNUSED(eventsCount);
	UNUSED(alpha);
}

void _spDrawOrderTimeline_dispose (spTimeline* timeline) {
	spDrawOrderTimeline* self = SUB_CAST(spDrawOrderTimeline, timeline);
	int i;

	_spTimeline_deinit(timeline);

	for (i = 0; i < self->framesCount; ++i)
		FREE(self->drawOrders[i]);
	FREE(self->drawOrders);
	FREE(self->frames);
	FREE(self);
}

spDrawOrderTimeline* spDrawOrderTimeline_create (int framesCount, int slotsCount) {
	spDrawOrderTimeline* self = NEW(spDrawOrderTimeline);
	_spTimeline_init(SUPER(self), SP_TIMELINE_DRAWORDER, _spDrawOrderTimeline_dispose, _spDrawOrderTimeline_apply);

	CONST_CAST(int, self->framesCount) = framesCount;
	CONST_CAST(float*, self->frames) = CALLOC(float, framesCount);
	CONST_CAST(int**, self->drawOrders) = CALLOC(int*, framesCount);
	CONST_CAST(int, self->slotsCount) = slotsCount;

	return self;
}

void spDrawOrderTimeline_setFrame (spDrawOrderTimeline* self, int frameIndex, float time, const int* drawOrder) {
	self->frames[frameIndex] = time;

	FREE(self->drawOrders[frameIndex]);
	if (!drawOrder)
		self->drawOrders[frameIndex] = 0;
	else {
		self->drawOrders[frameIndex] = MALLOC(int, self->slotsCount);
		memcpy(CONST_CAST(int*, self->drawOrders[frameIndex]), drawOrder, self->slotsCount * sizeof(int));
	}
}

/**/

void _spFFDTimeline_apply (const spTimeline* timeline, spSkeleton* skeleton, float lastTime, float time, spEvent** firedEvents,
		int* eventsCount, float alpha) {
	int frame, i, vertexCount;
	float percent, frameTime;
	const float* prevVertices;
	const float* nextVertices;
	spFFDTimeline* self = (spFFDTimeline*)timeline;

	spSlot *slot = skeleton->slots[self->slotIndex];

	if (slot->attachment != self->attachment) {
		if (!slot->attachment) return;
		switch (slot->attachment->type) {
		case SP_ATTACHMENT_MESH: {
			spMeshAttachment* mesh = SUB_CAST(spMeshAttachment, slot->attachment);
			if (!mesh->inheritFFD || mesh->parentMesh != (void*)self->attachment) return;
			break;
		}
		case SP_ATTACHMENT_WEIGHTED_MESH: {
			spWeightedMeshAttachment* mesh = SUB_CAST(spWeightedMeshAttachment, slot->attachment);
			if (!mesh->inheritFFD || mesh->parentMesh != (void*)self->attachment) return;
			break;
		}
		default:
			return;
		}
	}

	if (time < self->frames[0]) return; /* Time is before first frame. */

	vertexCount = self->frameVerticesCount;
	if (slot->attachmentVerticesCount < vertexCount) {
		if (slot->attachmentVerticesCapacity < vertexCount) {
			FREE(slot->attachmentVertices);
			slot->attachmentVertices = MALLOC(float, vertexCount);
			slot->attachmentVerticesCapacity = vertexCount;
		}
	}
	if (slot->attachmentVerticesCount != vertexCount) alpha = 1; /* Don't mix from uninitialized slot vertices. */
	slot->attachmentVerticesCount = vertexCount;

	if (time >= self->frames[self->framesCount - 1]) {
		/* Time is after last frame. */
		const float* lastVertices = self->frameVertices[self->framesCount - 1];
		if (alpha < 1) {
			for (i = 0; i < vertexCount; ++i)
				slot->attachmentVertices[i] += (lastVertices[i] - slot->attachmentVertices[i]) * alpha;
		} else
			memcpy(slot->attachmentVertices, lastVertices, vertexCount * sizeof(float));
		return;
	}

	/* Interpolate between the previous frame and the current frame. */
	frame = binarySearch1(self->frames, self->framesCount, time);
	frameTime = self->frames[frame];
	percent = 1 - (time - frameTime) / (self->frames[frame - 1] - frameTime);
	percent = spCurveTimeline_getCurvePercent(SUPER(self), frame - 1, percent < 0 ? 0 : (percent > 1 ? 1 : percent));

	prevVertices = self->frameVertices[frame - 1];
	nextVertices = self->frameVertices[frame];

	if (alpha < 1) {
		for (i = 0; i < vertexCount; ++i) {
			float prev = prevVertices[i];
			slot->attachmentVertices[i] += (prev + (nextVertices[i] - prev) * percent - slot->attachmentVertices[i]) * alpha;
		}
	} else {
		for (i = 0; i < vertexCount; ++i) {
			float prev = prevVertices[i];
			slot->attachmentVertices[i] = prev + (nextVertices[i] - prev) * percent;
		}
	}

	UNUSED(lastTime);
	UNUSED(firedEvents);
	UNUSED(eventsCount);
}

void _spFFDTimeline_dispose (spTimeline* timeline) {
	spFFDTimeline* self = SUB_CAST(spFFDTimeline, timeline);
	int i;

	_spCurveTimeline_deinit(SUPER(self));

	for (i = 0; i < self->framesCount; ++i)
		FREE(self->frameVertices[i]);
	FREE(self->frameVertices);
	FREE(self->frames);
	FREE(self);
}

spFFDTimeline* spFFDTimeline_create (int framesCount, int frameVerticesCount) {
	spFFDTimeline* self = NEW(spFFDTimeline);
	_spCurveTimeline_init(SUPER(self), SP_TIMELINE_FFD, framesCount, _spFFDTimeline_dispose, _spFFDTimeline_apply);
	CONST_CAST(int, self->framesCount) = framesCount;
	CONST_CAST(float*, self->frames) = CALLOC(float, self->framesCount);
	CONST_CAST(float**, self->frameVertices) = CALLOC(float*, framesCount);
	CONST_CAST(int, self->frameVerticesCount) = frameVerticesCount;
	return self;
}

void spFFDTimeline_setFrame (spFFDTimeline* self, int frameIndex, float time, float* vertices) {
	self->frames[frameIndex] = time;

	FREE(self->frameVertices[frameIndex]);
	if (!vertices)
		self->frameVertices[frameIndex] = 0;
	else {
		self->frameVertices[frameIndex] = MALLOC(float, self->frameVerticesCount);
		memcpy(CONST_CAST(float*, self->frameVertices[frameIndex]), vertices, self->frameVerticesCount * sizeof(float));
	}
}


/**/

static const int IKCONSTRAINT_PREV_TIME = -3;
static const int IKCONSTRAINT_PREV_MIX = -2;
static const int IKCONSTRAINT_PREV_BEND_DIRECTION = -1;
static const int IKCONSTRAINT_MIX = 1;

void _spIkConstraintTimeline_apply (const spTimeline* timeline, spSkeleton* skeleton, float lastTime, float time,
		spEvent** firedEvents, int* eventsCount, float alpha) {
	int frame;
	float frameTime, percent, mix;
	spIkConstraint* constraint;
	spIkConstraintTimeline* self = (spIkConstraintTimeline*)timeline;

	if (time < self->frames[0]) return; /* Time is before first frame. */

	constraint = skeleton->ikConstraints[self->ikConstraintIndex];

	if (time >= self->frames[self->framesCount - 3]) { /* Time is after last frame. */
		constraint->mix += (self->frames[self->framesCount + IKCONSTRAINT_PREV_MIX] - constraint->mix) * alpha;
		constraint->bendDirection = (int)self->frames[self->framesCount + IKCONSTRAINT_PREV_BEND_DIRECTION];
		return;
	}

	/* Interpolate between the previous frame and the current frame. */
	frame = binarySearch(self->frames, self->framesCount, time, 3);
	frameTime = self->frames[frame];
	percent = 1 - (time - frameTime) / (self->frames[frame + IKCONSTRAINT_PREV_TIME] - frameTime);
	percent = spCurveTimeline_getCurvePercent(SUPER(self), frame / 3 - 1, percent < 0 ? 0 : (percent > 1 ? 1 : percent));

	mix = self->frames[frame + IKCONSTRAINT_PREV_MIX];
	constraint->mix += (mix + (self->frames[frame + IKCONSTRAINT_MIX] - mix) * percent - constraint->mix) * alpha;
	constraint->bendDirection = (int)self->frames[frame + IKCONSTRAINT_PREV_BEND_DIRECTION];

	UNUSED(lastTime);
	UNUSED(firedEvents);
	UNUSED(eventsCount);
}

spIkConstraintTimeline* spIkConstraintTimeline_create (int framesCount) {
	return (spIkConstraintTimeline*)_spBaseTimeline_create(framesCount, SP_TIMELINE_IKCONSTRAINT, 3, _spIkConstraintTimeline_apply);
}

void spIkConstraintTimeline_setFrame (spIkConstraintTimeline* self, int frameIndex, float time, float mix, int bendDirection) {
	frameIndex *= 3;
	self->frames[frameIndex] = time;
	self->frames[frameIndex + 1] = mix;
	self->frames[frameIndex + 2] = (float)bendDirection;
}

/**/
static const int TRANSFORMCONSTRAINT_PREV_TIME = -5;
static const int TRANSFORMCONSTRAINT_PREV_ROTATE_MIX = -4;
static const int TRANSFORMCONSTRAINT_PREV_TRANSLATE_MIX = -3;
static const int TRANSFORMCONSTRAINT_PREV_SCALE_MIX = -2;
static const int TRANSFORMCONSTRAINT_PREV_SHEAR_MIX = -1;
static const int TRANSFORMCONSTRAINT_ROTATE_MIX = 1;
static const int TRANSFORMCONSTRAINT_TRANSLATE_MIX = 2;
static const int TRANSFORMCONSTRAINT_SCALE_MIX = 3;
static const int TRANSFORMCONSTRAINT_SHEAR_MIX = 4;

void _spTransformConstraintTimeline_apply (const spTimeline* timeline, spSkeleton* skeleton, float lastTime, float time,
									spEvent** firedEvents, int* eventsCount, float alpha) {
	int frame;
	float frameTime, percent, rotate, translate, scale, shear;
	spTransformConstraint* constraint;
	spTransformConstraintTimeline* self = (spTransformConstraintTimeline*)timeline;

	if (time < self->frames[0]) return; /* Time is before first frame. */

	constraint = skeleton->transformConstraints[self->transformConstraintIndex];

	if (time >= self->frames[self->framesCount - 5]) { /* Time is after last frame. */
		int len = self->framesCount;
		constraint->rotateMix += (self->frames[len + TRANSFORMCONSTRAINT_PREV_ROTATE_MIX] - constraint->rotateMix) * alpha;
		constraint->translateMix += (self->frames[len + TRANSFORMCONSTRAINT_PREV_TRANSLATE_MIX] - constraint->translateMix) * alpha;
		constraint->scaleMix += (self->frames[len + TRANSFORMCONSTRAINT_PREV_SCALE_MIX] - constraint->scaleMix) * alpha;
		constraint->shearMix += (self->frames[len + TRANSFORMCONSTRAINT_PREV_SHEAR_MIX] - constraint->shearMix) * alpha;
		return;
	}

	/* Interpolate between the previous frame and the current frame. */
	frame = binarySearch(self->frames, self->framesCount, time, 5);
	frameTime = self->frames[frame];
	percent = 1 - (time - frameTime) / (self->frames[frame + IKCONSTRAINT_PREV_TIME] - frameTime);
	percent = spCurveTimeline_getCurvePercent(SUPER(self), frame / 5 - 1, percent < 0 ? 0 : (percent > 1 ? 1 : percent));

	rotate = self->frames[frame + TRANSFORMCONSTRAINT_PREV_ROTATE_MIX];
	translate = self->frames[frame + TRANSFORMCONSTRAINT_PREV_TRANSLATE_MIX];
	scale = self->frames[frame + TRANSFORMCONSTRAINT_PREV_SCALE_MIX];
	shear = self->frames[frame + TRANSFORMCONSTRAINT_PREV_SHEAR_MIX];
	constraint->rotateMix += (rotate + (self->frames[frame + TRANSFORMCONSTRAINT_ROTATE_MIX] - rotate) * percent - constraint->rotateMix) * alpha;
	constraint->translateMix += (translate + (self->frames[frame + TRANSFORMCONSTRAINT_TRANSLATE_MIX] - translate) * percent - constraint->translateMix)
							   * alpha;
	constraint->scaleMix += (scale + (self->frames[frame + TRANSFORMCONSTRAINT_SCALE_MIX] - scale) * percent - constraint->scaleMix) * alpha;
	constraint->shearMix += (shear + (self->frames[frame + TRANSFORMCONSTRAINT_SHEAR_MIX] - shear) * percent - constraint->shearMix) * alpha;

	UNUSED(lastTime);
	UNUSED(firedEvents);
	UNUSED(eventsCount);
}

spTransformConstraintTimeline* spTransformConstraintTimeline_create (int framesCount) {
	return (spTransformConstraintTimeline*)_spBaseTimeline_create(framesCount, SP_TIMELINE_TRANSFORMCONSTRAINT, 5, _spTransformConstraintTimeline_apply);
}

void spTransformConstraintTimeline_setFrame (spTransformConstraintTimeline* self, int frameIndex, float time, float rotateMix, float translateMix, float scaleMix, float shearMix) {
	frameIndex *= 5;
	self->frames[frameIndex] = time;
	self->frames[frameIndex + 1] = rotateMix;
	self->frames[frameIndex + 2] = translateMix;
	self->frames[frameIndex + 3] = scaleMix;
	self->frames[frameIndex + 4] = shearMix;
}

/**/
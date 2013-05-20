// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/renderer/gpu/input_handler_proxy.h"

#include "base/basictypes.h"
#include "base/memory/scoped_ptr.h"
#include "content/renderer/gpu/input_handler_proxy_client.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/WebKit/Source/Platform/chromium/public/WebFloatPoint.h"
#include "third_party/WebKit/Source/Platform/chromium/public/WebFloatSize.h"
#include "third_party/WebKit/Source/Platform/chromium/public/WebGestureCurve.h"
#include "third_party/WebKit/Source/Platform/chromium/public/WebPoint.h"
#include "third_party/WebKit/Source/WebKit/chromium/public/WebInputEvent.h"

using WebKit::WebActiveWheelFlingParameters;
using WebKit::WebFloatPoint;
using WebKit::WebFloatSize;
using WebKit::WebGestureEvent;
using WebKit::WebInputEvent;
using WebKit::WebMouseWheelEvent;
using WebKit::WebPoint;
using WebKit::WebSize;

namespace {

class MockInputHandler : public cc::InputHandler {
 public:
  MockInputHandler() {}
  virtual ~MockInputHandler() {}

  MOCK_METHOD0(PinchGestureBegin, void());
  MOCK_METHOD2(PinchGestureUpdate,
               void(float magnify_delta, gfx::Point anchor));
  MOCK_METHOD0(PinchGestureEnd, void());

  MOCK_METHOD0(ScheduleAnimation, void());

  MOCK_METHOD2(ScrollBegin,
               ScrollStatus(gfx::Point viewport_point,
                            cc::InputHandler::ScrollInputType type));
  MOCK_METHOD2(ScrollBy,
               bool(gfx::Point viewport_point, gfx::Vector2dF scroll_delta));
  MOCK_METHOD2(ScrollVerticallyByPage,
               bool(gfx::Point viewport_point,
                    WebKit::WebScrollbar::ScrollDirection direction));
  MOCK_METHOD0(ScrollEnd, void());
  MOCK_METHOD0(FlingScrollBegin, cc::InputHandler::ScrollStatus());

  MOCK_METHOD1(DidReceiveLastInputEventForVSync, void(base::TimeTicks time));

  virtual void BindToClient(cc::InputHandlerClient* client) OVERRIDE {}

  virtual void StartPageScaleAnimation(gfx::Vector2d target_offset,
                                       bool anchor_point,
                                       float page_scale,
                                       base::TimeTicks start_time,
                                       base::TimeDelta duration) OVERRIDE {}

  virtual void NotifyCurrentFlingVelocity(gfx::Vector2dF velocity) OVERRIDE {}

  virtual bool HaveTouchEventHandlersAt(gfx::Point point) OVERRIDE {
    return false;
  }

  virtual void SetRootLayerScrollOffsetDelegate(
      cc::LayerScrollOffsetDelegate* root_layer_scroll_offset_delegate)
      OVERRIDE {}

  virtual void OnRootLayerDelegatedScrollOffsetChanged() OVERRIDE {}

  DISALLOW_COPY_AND_ASSIGN(MockInputHandler);
};

// A simple WebGestureCurve implementation that flings at a constant velocity
// indefinitely.
class FakeWebGestureCurve : public WebKit::WebGestureCurve {
 public:
  FakeWebGestureCurve(const WebKit::WebFloatPoint& velocity,
                      const WebKit::WebSize& cumulative_scroll)
      : velocity_(velocity), cumulative_scroll_(cumulative_scroll) {}

  virtual ~FakeWebGestureCurve() {}

  // Returns false if curve has finished and can no longer be applied.
  virtual bool apply(double time, WebKit::WebGestureCurveTarget* target) {
    WebKit::WebSize displacement(velocity_.x * time, velocity_.y * time);
    WebKit::WebFloatSize increment(
        displacement.width - cumulative_scroll_.width,
        displacement.height - cumulative_scroll_.height);
    cumulative_scroll_ = displacement;
    // scrollBy() could delete this curve if the animation is over, so don't
    // touch any member variables after making that call.
    target->scrollBy(increment);
    return true;
  }

 private:
  WebKit::WebFloatPoint velocity_;
  WebKit::WebSize cumulative_scroll_;

  DISALLOW_COPY_AND_ASSIGN(FakeWebGestureCurve);
};

class MockInputHandlerProxyClient
    : public content::InputHandlerProxyClient {
 public:
  MockInputHandlerProxyClient() {}
  virtual ~MockInputHandlerProxyClient() {}

  virtual void WillShutdown() OVERRIDE {}

  MOCK_METHOD0(DidHandleInputEvent, void());
  MOCK_METHOD1(DidNotHandleInputEvent, void(bool send_to_widget));

  MOCK_METHOD1(TransferActiveWheelFlingAnimation,
               void(const WebActiveWheelFlingParameters&));

  virtual WebKit::WebGestureCurve* CreateFlingAnimationCurve(
      int deviceSource,
      const WebFloatPoint& velocity,
      const WebSize& cumulative_scroll) OVERRIDE {
    return new FakeWebGestureCurve(velocity, cumulative_scroll);
  }

  virtual void DidOverscroll(gfx::Vector2dF overscroll,
                             gfx::Vector2dF fling_velocity) {}

 private:
  DISALLOW_COPY_AND_ASSIGN(MockInputHandlerProxyClient);
};

class InputHandlerProxyTest : public testing::Test {
 public:
  InputHandlerProxyTest() : expected_disposition_(DidHandle) {
    input_handler_.reset(
        new content::InputHandlerProxy(&mock_input_handler_));
    input_handler_->SetClient(&mock_client_);
  }

  ~InputHandlerProxyTest() {
    input_handler_.reset();
  }

// This is defined as a macro because when an expectation is not satisfied the
// only output you get
// out of gmock is the line number that set the expectation.
#define VERIFY_AND_RESET_MOCKS()                                              \
  do {                                                                        \
    testing::Mock::VerifyAndClearExpectations(&mock_input_handler_);          \
    testing::Mock::VerifyAndClearExpectations(&mock_client_);                 \
    switch (expected_disposition_) {                                          \
      case DidHandle:                                                         \
        /* If we expect to handle events, we shouldn't get any */             \
        /* DidNotHandleInputEvent() calls with any parameter. */              \
        EXPECT_CALL(mock_client_, DidNotHandleInputEvent(::testing::_))       \
            .Times(0);                                                        \
        EXPECT_CALL(mock_client_, DidHandleInputEvent());                     \
        break;                                                                \
      case DidNotHandle:                                                      \
        /* If we aren't expecting to handle events, we shouldn't call      */ \
        /* DidHandleInputEvent(). */                                          \
        EXPECT_CALL(mock_client_, DidHandleInputEvent()).Times(0);            \
        EXPECT_CALL(mock_client_, DidNotHandleInputEvent(false)).Times(0);    \
        EXPECT_CALL(mock_client_, DidNotHandleInputEvent(true));              \
        break;                                                                \
      case DropEvent:                                                         \
        /* If we're expecting to drop, we shouldn't get any didHandle..() */  \
        /* or DidNotHandleInputEvent(true) calls. */                          \
        EXPECT_CALL(mock_client_, DidHandleInputEvent()).Times(0);            \
        EXPECT_CALL(mock_client_, DidNotHandleInputEvent(true)).Times(0);     \
        EXPECT_CALL(mock_client_, DidNotHandleInputEvent(false));             \
        break;                                                                \
    }                                                                         \
  } while (false)

 protected:
  testing::StrictMock<MockInputHandler> mock_input_handler_;
  scoped_ptr<content::InputHandlerProxy> input_handler_;
  testing::StrictMock<MockInputHandlerProxyClient> mock_client_;
  WebGestureEvent gesture_;

  enum ExpectedDisposition {
    DidHandle,
    DidNotHandle,
    DropEvent
  };
  ExpectedDisposition expected_disposition_;
};

TEST_F(InputHandlerProxyTest, MouseWheelByPageMainThread) {
  WebMouseWheelEvent wheel;
  wheel.type = WebInputEvent::MouseWheel;
  wheel.scrollByPage = true;

  EXPECT_CALL(mock_client_, DidNotHandleInputEvent(true)).Times(1);
  input_handler_->HandleInputEvent(wheel);
  testing::Mock::VerifyAndClearExpectations(&mock_client_);
}

TEST_F(InputHandlerProxyTest, GestureScrollStarted) {
  // We shouldn't send any events to the widget for this gesture.
  expected_disposition_ = DidHandle;
  VERIFY_AND_RESET_MOCKS();

  EXPECT_CALL(mock_input_handler_, ScrollBegin(testing::_, testing::_))
      .WillOnce(testing::Return(cc::InputHandler::ScrollStarted));

  gesture_.type = WebInputEvent::GestureScrollBegin;
  input_handler_->HandleInputEvent(gesture_);

  // The event should not be marked as handled if scrolling is not possible.
  expected_disposition_ = DropEvent;
  VERIFY_AND_RESET_MOCKS();

  gesture_.type = WebInputEvent::GestureScrollUpdate;
  gesture_.data.scrollUpdate.deltaY =
      -40;  // -Y means scroll down - i.e. in the +Y direction.
  EXPECT_CALL(mock_input_handler_,
              ScrollBy(testing::_,
                       testing::Property(&gfx::Vector2dF::y, testing::Gt(0))))
      .WillOnce(testing::Return(false));
  input_handler_->HandleInputEvent(gesture_);

  // Mark the event as handled if scroll happens.
  expected_disposition_ = DidHandle;
  VERIFY_AND_RESET_MOCKS();

  gesture_.type = WebInputEvent::GestureScrollUpdate;
  gesture_.data.scrollUpdate.deltaY =
      -40;  // -Y means scroll down - i.e. in the +Y direction.
  EXPECT_CALL(mock_input_handler_,
              ScrollBy(testing::_,
                       testing::Property(&gfx::Vector2dF::y, testing::Gt(0))))
      .WillOnce(testing::Return(true));
  input_handler_->HandleInputEvent(gesture_);

  VERIFY_AND_RESET_MOCKS();

  gesture_.type = WebInputEvent::GestureScrollEnd;
  gesture_.data.scrollUpdate.deltaY = 0;
  EXPECT_CALL(mock_input_handler_, ScrollEnd());
  input_handler_->HandleInputEvent(gesture_);
}

TEST_F(InputHandlerProxyTest, GestureScrollOnMainThread) {
  // We should send all events to the widget for this gesture.
  expected_disposition_ = DidNotHandle;
  VERIFY_AND_RESET_MOCKS();

  EXPECT_CALL(mock_input_handler_, ScrollBegin(::testing::_, ::testing::_))
      .WillOnce(testing::Return(cc::InputHandler::ScrollOnMainThread));

  gesture_.type = WebInputEvent::GestureScrollBegin;
  input_handler_->HandleInputEvent(gesture_);

  VERIFY_AND_RESET_MOCKS();

  gesture_.type = WebInputEvent::GestureScrollUpdate;
  gesture_.data.scrollUpdate.deltaY = 40;
  input_handler_->HandleInputEvent(gesture_);

  VERIFY_AND_RESET_MOCKS();

  gesture_.type = WebInputEvent::GestureScrollEnd;
  gesture_.data.scrollUpdate.deltaY = 0;
  input_handler_->HandleInputEvent(gesture_);
}

TEST_F(InputHandlerProxyTest, GestureScrollIgnored) {
  // We shouldn't handle the GestureScrollBegin.
  // Instead, we should get one DidNotHandleInputEvent(false) call per
  // HandleInputEvent(), indicating that we could determine that there's nothing
  // that could scroll or otherwise react to this gesture sequence and thus we
  // should drop the whole gesture sequence on the floor.
  expected_disposition_ = DropEvent;
  VERIFY_AND_RESET_MOCKS();

  EXPECT_CALL(mock_input_handler_, ScrollBegin(testing::_, testing::_))
      .WillOnce(testing::Return(cc::InputHandler::ScrollIgnored));

  gesture_.type = WebInputEvent::GestureScrollBegin;
  input_handler_->HandleInputEvent(gesture_);
}

TEST_F(InputHandlerProxyTest, GesturePinch) {
  // We shouldn't send any events to the widget for this gesture.
  expected_disposition_ = DidHandle;
  VERIFY_AND_RESET_MOCKS();

  gesture_.type = WebInputEvent::GesturePinchBegin;
  EXPECT_CALL(mock_input_handler_, PinchGestureBegin());
  input_handler_->HandleInputEvent(gesture_);

  VERIFY_AND_RESET_MOCKS();

  gesture_.type = WebInputEvent::GesturePinchUpdate;
  gesture_.data.pinchUpdate.scale = 1.5;
  gesture_.x = 7;
  gesture_.y = 13;
  EXPECT_CALL(mock_input_handler_, PinchGestureUpdate(1.5, gfx::Point(7, 13)));
  input_handler_->HandleInputEvent(gesture_);

  VERIFY_AND_RESET_MOCKS();

  gesture_.type = WebInputEvent::GesturePinchUpdate;
  gesture_.data.pinchUpdate.scale = 0.5;
  gesture_.x = 9;
  gesture_.y = 6;
  EXPECT_CALL(mock_input_handler_, PinchGestureUpdate(.5, gfx::Point(9, 6)));
  input_handler_->HandleInputEvent(gesture_);

  VERIFY_AND_RESET_MOCKS();

  gesture_.type = WebInputEvent::GesturePinchEnd;
  EXPECT_CALL(mock_input_handler_, PinchGestureEnd());
  input_handler_->HandleInputEvent(gesture_);
}

TEST_F(InputHandlerProxyTest, GesturePinchAfterScrollOnMainThread) {
  // Scrolls will start by being sent to the main thread.
  expected_disposition_ = DidNotHandle;
  VERIFY_AND_RESET_MOCKS();

  EXPECT_CALL(mock_input_handler_, ScrollBegin(::testing::_, ::testing::_))
      .WillOnce(testing::Return(cc::InputHandler::ScrollOnMainThread));

  gesture_.type = WebInputEvent::GestureScrollBegin;
  input_handler_->HandleInputEvent(gesture_);

  VERIFY_AND_RESET_MOCKS();

  gesture_.type = WebInputEvent::GestureScrollUpdate;
  gesture_.data.scrollUpdate.deltaY = 40;
  input_handler_->HandleInputEvent(gesture_);

  // However, after the pinch gesture starts, they should go to the impl
  // thread.
  expected_disposition_ = DidHandle;
  VERIFY_AND_RESET_MOCKS();

  gesture_.type = WebInputEvent::GesturePinchBegin;
  EXPECT_CALL(mock_input_handler_, PinchGestureBegin());
  input_handler_->HandleInputEvent(gesture_);

  VERIFY_AND_RESET_MOCKS();

  gesture_.type = WebInputEvent::GesturePinchUpdate;
  gesture_.data.pinchUpdate.scale = 1.5;
  gesture_.x = 7;
  gesture_.y = 13;
  EXPECT_CALL(mock_input_handler_, PinchGestureUpdate(1.5, gfx::Point(7, 13)));
  input_handler_->HandleInputEvent(gesture_);

  VERIFY_AND_RESET_MOCKS();

  gesture_.type = WebInputEvent::GestureScrollUpdate;
  gesture_.data.scrollUpdate.deltaY =
      -40;  // -Y means scroll down - i.e. in the +Y direction.
  EXPECT_CALL(mock_input_handler_,
              ScrollBy(testing::_,
                       testing::Property(&gfx::Vector2dF::y, testing::Gt(0))))
      .WillOnce(testing::Return(true));
  input_handler_->HandleInputEvent(gesture_);

  VERIFY_AND_RESET_MOCKS();

  gesture_.type = WebInputEvent::GesturePinchUpdate;
  gesture_.data.pinchUpdate.scale = 0.5;
  gesture_.x = 9;
  gesture_.y = 6;
  EXPECT_CALL(mock_input_handler_, PinchGestureUpdate(.5, gfx::Point(9, 6)));
  input_handler_->HandleInputEvent(gesture_);

  VERIFY_AND_RESET_MOCKS();

  gesture_.type = WebInputEvent::GesturePinchEnd;
  EXPECT_CALL(mock_input_handler_, PinchGestureEnd());
  input_handler_->HandleInputEvent(gesture_);

  // After the pinch gesture ends, they should go to back to the main
  // thread.
  expected_disposition_ = DidNotHandle;
  VERIFY_AND_RESET_MOCKS();

  gesture_.type = WebInputEvent::GestureScrollEnd;
  gesture_.data.scrollUpdate.deltaY = 0;
  input_handler_->HandleInputEvent(gesture_);
}

TEST_F(InputHandlerProxyTest, GestureFlingStartedTouchpad) {
  // We shouldn't send any events to the widget for this gesture.
  expected_disposition_ = DidHandle;
  VERIFY_AND_RESET_MOCKS();

  EXPECT_CALL(mock_input_handler_, ScrollBegin(testing::_, testing::_))
      .WillOnce(testing::Return(cc::InputHandler::ScrollStarted));
  EXPECT_CALL(mock_input_handler_, ScrollEnd());
  EXPECT_CALL(mock_input_handler_, ScheduleAnimation());

  gesture_.type = WebInputEvent::GestureFlingStart;
  gesture_.data.flingStart.velocityX = 10;
  gesture_.sourceDevice = WebGestureEvent::Touchpad;
  input_handler_->HandleInputEvent(gesture_);

  VERIFY_AND_RESET_MOCKS();

  // Verify that a GestureFlingCancel during an animation cancels it.
  gesture_.type = WebInputEvent::GestureFlingCancel;
  gesture_.sourceDevice = WebGestureEvent::Touchpad;
  input_handler_->HandleInputEvent(gesture_);
}

TEST_F(InputHandlerProxyTest, GestureFlingOnMainThreadTouchpad) {
  // We should send all events to the widget for this gesture.
  expected_disposition_ = DidNotHandle;
  VERIFY_AND_RESET_MOCKS();

  EXPECT_CALL(mock_input_handler_, ScrollBegin(testing::_, testing::_))
      .WillOnce(testing::Return(cc::InputHandler::ScrollOnMainThread));

  gesture_.type = WebInputEvent::GestureFlingStart;
  gesture_.sourceDevice = WebGestureEvent::Touchpad;
  input_handler_->HandleInputEvent(gesture_);

  // Since we returned ScrollStatusOnMainThread from scrollBegin, ensure the
  // input handler knows it's scrolling off the impl thread
  ASSERT_FALSE(input_handler_->gesture_scroll_on_impl_thread_for_testing());

  VERIFY_AND_RESET_MOCKS();

  // Even if we didn't start a fling ourselves, we still need to send the cancel
  // event to the widget.
  gesture_.type = WebInputEvent::GestureFlingCancel;
  gesture_.sourceDevice = WebGestureEvent::Touchpad;
  input_handler_->HandleInputEvent(gesture_);
}

TEST_F(InputHandlerProxyTest, GestureFlingIgnoredTouchpad) {
  expected_disposition_ = DidNotHandle;
  VERIFY_AND_RESET_MOCKS();

  EXPECT_CALL(mock_input_handler_, ScrollBegin(testing::_, testing::_))
      .WillOnce(testing::Return(cc::InputHandler::ScrollIgnored));

  gesture_.type = WebInputEvent::GestureFlingStart;
  gesture_.sourceDevice = WebGestureEvent::Touchpad;
  input_handler_->HandleInputEvent(gesture_);

  expected_disposition_ = DropEvent;
  VERIFY_AND_RESET_MOCKS();

  // Since the previous fling was ignored, we should also be dropping the next
  // fling_cancel.
  gesture_.type = WebInputEvent::GestureFlingCancel;
  gesture_.sourceDevice = WebGestureEvent::Touchpad;
  input_handler_->HandleInputEvent(gesture_);
}

TEST_F(InputHandlerProxyTest, GestureFlingAnimatesTouchpad) {
  // We shouldn't send any events to the widget for this gesture.
  expected_disposition_ = DidHandle;
  VERIFY_AND_RESET_MOCKS();

  // On the fling start, we should schedule an animation but not actually start
  // scrolling.
  gesture_.type = WebInputEvent::GestureFlingStart;
  WebFloatPoint fling_delta = WebFloatPoint(1000, 0);
  WebPoint fling_point = WebPoint(7, 13);
  WebPoint fling_global_point = WebPoint(17, 23);
  int modifiers = 7;
  gesture_.data.flingStart.velocityX = fling_delta.x;
  gesture_.data.flingStart.velocityY = fling_delta.y;
  gesture_.sourceDevice = WebGestureEvent::Touchpad;
  gesture_.x = fling_point.x;
  gesture_.y = fling_point.y;
  gesture_.globalX = fling_global_point.x;
  gesture_.globalY = fling_global_point.y;
  gesture_.modifiers = modifiers;
  EXPECT_CALL(mock_input_handler_, ScheduleAnimation());
  EXPECT_CALL(mock_input_handler_, ScrollBegin(testing::_, testing::_))
      .WillOnce(testing::Return(cc::InputHandler::ScrollStarted));
  EXPECT_CALL(mock_input_handler_, ScrollEnd());
  input_handler_->HandleInputEvent(gesture_);

  testing::Mock::VerifyAndClearExpectations(&mock_input_handler_);
  // The first animate call should let us pick up an animation start time, but
  // we shouldn't actually move anywhere just yet. The first frame after the
  // fling start will typically include the last scroll from the gesture that
  // lead to the scroll (either wheel or gesture scroll), so there should be no
  // visible hitch.
  EXPECT_CALL(mock_input_handler_, ScheduleAnimation());
  EXPECT_CALL(mock_input_handler_, ScrollBegin(testing::_, testing::_))
      .Times(0);
  base::TimeTicks time = base::TimeTicks() + base::TimeDelta::FromSeconds(10);
  input_handler_->Animate(time);

  testing::Mock::VerifyAndClearExpectations(&mock_input_handler_);

  // The second call should start scrolling in the -X direction.
  EXPECT_CALL(mock_input_handler_, ScheduleAnimation());
  EXPECT_CALL(mock_input_handler_, ScrollBegin(testing::_, testing::_))
      .WillOnce(testing::Return(cc::InputHandler::ScrollStarted));
  EXPECT_CALL(mock_input_handler_,
              ScrollBy(testing::_,
                       testing::Property(&gfx::Vector2dF::x, testing::Lt(0))))
      .WillOnce(testing::Return(true));
  EXPECT_CALL(mock_input_handler_, ScrollEnd());
  time += base::TimeDelta::FromMilliseconds(100);
  input_handler_->Animate(time);

  testing::Mock::VerifyAndClearExpectations(&mock_input_handler_);

  // Let's say on the third call we hit a non-scrollable region. We should abort
  // the fling and not scroll.
  // We also should pass the current fling parameters out to the client so the
  // rest of the fling can be
  // transferred to the main thread.
  EXPECT_CALL(mock_input_handler_, ScheduleAnimation());
  EXPECT_CALL(mock_input_handler_, ScrollBegin(testing::_, testing::_))
      .WillOnce(testing::Return(cc::InputHandler::ScrollOnMainThread));
  EXPECT_CALL(mock_input_handler_, ScrollBy(testing::_, testing::_)).Times(0);
  EXPECT_CALL(mock_input_handler_, ScrollEnd()).Times(0);
  // Expected wheel fling animation parameters:
  // *) fling_delta and fling_point should match the original GestureFlingStart
  // event
  // *) startTime should be 10 to match the time parameter of the first
  // Animate() call after the GestureFlingStart
  // *) cumulativeScroll depends on the curve, but since we've animated in the
  // -X direction the X value should be < 0
  EXPECT_CALL(
      mock_client_,
      TransferActiveWheelFlingAnimation(testing::AllOf(
          testing::Field(&WebActiveWheelFlingParameters::delta,
                         testing::Eq(fling_delta)),
          testing::Field(&WebActiveWheelFlingParameters::point,
                         testing::Eq(fling_point)),
          testing::Field(&WebActiveWheelFlingParameters::globalPoint,
                         testing::Eq(fling_global_point)),
          testing::Field(&WebActiveWheelFlingParameters::modifiers,
                         testing::Eq(modifiers)),
          testing::Field(&WebActiveWheelFlingParameters::startTime,
                         testing::Eq(10)),
          testing::Field(&WebActiveWheelFlingParameters::cumulativeScroll,
                         testing::Field(&WebSize::width, testing::Gt(0))))));
  time += base::TimeDelta::FromMilliseconds(100);
  input_handler_->Animate(time);

  testing::Mock::VerifyAndClearExpectations(&mock_input_handler_);
  testing::Mock::VerifyAndClearExpectations(&mock_client_);

  // Since we've aborted the fling, the next animation should be a no-op and
  // should not result in another
  // frame being requested.
  EXPECT_CALL(mock_input_handler_, ScheduleAnimation()).Times(0);
  EXPECT_CALL(mock_input_handler_, ScrollBegin(testing::_, testing::_))
      .Times(0);
  time += base::TimeDelta::FromMilliseconds(100);
  input_handler_->Animate(time);

  // Since we've transferred the fling to the main thread, we need to pass the
  // next GestureFlingCancel to the main
  // thread as well.
  EXPECT_CALL(mock_client_, DidNotHandleInputEvent(true));
  gesture_.type = WebInputEvent::GestureFlingCancel;
  input_handler_->HandleInputEvent(gesture_);
}

TEST_F(InputHandlerProxyTest, GestureFlingTransferResetsTouchpad) {
  // We shouldn't send any events to the widget for this gesture.
  expected_disposition_ = DidHandle;
  VERIFY_AND_RESET_MOCKS();

  // Start a gesture fling in the -X direction with zero Y movement.
  gesture_.type = WebInputEvent::GestureFlingStart;
  WebFloatPoint fling_delta = WebFloatPoint(1000, 0);
  WebPoint fling_point = WebPoint(7, 13);
  WebPoint fling_global_point = WebPoint(17, 23);
  int modifiers = 1;
  gesture_.data.flingStart.velocityX = fling_delta.x;
  gesture_.data.flingStart.velocityY = fling_delta.y;
  gesture_.sourceDevice = WebGestureEvent::Touchpad;
  gesture_.x = fling_point.x;
  gesture_.y = fling_point.y;
  gesture_.globalX = fling_global_point.x;
  gesture_.globalY = fling_global_point.y;
  gesture_.modifiers = modifiers;
  EXPECT_CALL(mock_input_handler_, ScheduleAnimation());
  EXPECT_CALL(mock_input_handler_, ScrollBegin(testing::_, testing::_))
      .WillOnce(testing::Return(cc::InputHandler::ScrollStarted));
  EXPECT_CALL(mock_input_handler_, ScrollEnd());
  input_handler_->HandleInputEvent(gesture_);

  testing::Mock::VerifyAndClearExpectations(&mock_input_handler_);

  // Start the fling animation at time 10. This shouldn't actually scroll, just
  // establish a start time.
  EXPECT_CALL(mock_input_handler_, ScheduleAnimation());
  EXPECT_CALL(mock_input_handler_, ScrollBegin(testing::_, testing::_))
      .Times(0);
  base::TimeTicks time = base::TimeTicks() + base::TimeDelta::FromSeconds(10);
  input_handler_->Animate(time);

  testing::Mock::VerifyAndClearExpectations(&mock_input_handler_);

  // The second call should start scrolling in the -X direction.
  EXPECT_CALL(mock_input_handler_, ScheduleAnimation());
  EXPECT_CALL(mock_input_handler_, ScrollBegin(testing::_, testing::_))
      .WillOnce(testing::Return(cc::InputHandler::ScrollStarted));
  EXPECT_CALL(mock_input_handler_,
              ScrollBy(testing::_,
                       testing::Property(&gfx::Vector2dF::x, testing::Lt(0))))
      .WillOnce(testing::Return(true));
  EXPECT_CALL(mock_input_handler_, ScrollEnd());
  time += base::TimeDelta::FromMilliseconds(100);
  input_handler_->Animate(time);

  testing::Mock::VerifyAndClearExpectations(&mock_input_handler_);

  // Let's say on the third call we hit a non-scrollable region. We should abort
  // the fling and not scroll.
  // We also should pass the current fling parameters out to the client so the
  // rest of the fling can be
  // transferred to the main thread.
  EXPECT_CALL(mock_input_handler_, ScheduleAnimation());
  EXPECT_CALL(mock_input_handler_, ScrollBegin(testing::_, testing::_))
      .WillOnce(testing::Return(cc::InputHandler::ScrollOnMainThread));
  EXPECT_CALL(mock_input_handler_, ScrollBy(testing::_, testing::_)).Times(0);
  EXPECT_CALL(mock_input_handler_, ScrollEnd()).Times(0);

  // Expected wheel fling animation parameters:
  // *) fling_delta and fling_point should match the original GestureFlingStart
  // event
  // *) startTime should be 10 to match the time parameter of the first
  // Animate() call after the GestureFlingStart
  // *) cumulativeScroll depends on the curve, but since we've animated in the
  // -X direction the X value should be < 0
  EXPECT_CALL(
      mock_client_,
      TransferActiveWheelFlingAnimation(testing::AllOf(
          testing::Field(&WebActiveWheelFlingParameters::delta,
                         testing::Eq(fling_delta)),
          testing::Field(&WebActiveWheelFlingParameters::point,
                         testing::Eq(fling_point)),
          testing::Field(&WebActiveWheelFlingParameters::globalPoint,
                         testing::Eq(fling_global_point)),
          testing::Field(&WebActiveWheelFlingParameters::modifiers,
                         testing::Eq(modifiers)),
          testing::Field(&WebActiveWheelFlingParameters::startTime,
                         testing::Eq(10)),
          testing::Field(&WebActiveWheelFlingParameters::cumulativeScroll,
                         testing::Field(&WebSize::width, testing::Gt(0))))));
  time += base::TimeDelta::FromMilliseconds(100);
  input_handler_->Animate(time);

  testing::Mock::VerifyAndClearExpectations(&mock_input_handler_);
  testing::Mock::VerifyAndClearExpectations(&mock_client_);

  // Since we've aborted the fling, the next animation should be a no-op and
  // should not result in another
  // frame being requested.
  EXPECT_CALL(mock_input_handler_, ScheduleAnimation()).Times(0);
  EXPECT_CALL(mock_input_handler_, ScrollBegin(testing::_, testing::_))
      .Times(0);
  time += base::TimeDelta::FromMilliseconds(100);
  input_handler_->Animate(time);

  testing::Mock::VerifyAndClearExpectations(&mock_input_handler_);

  // Since we've transferred the fling to the main thread, we need to pass the
  // next GestureFlingCancel to the main
  // thread as well.
  EXPECT_CALL(mock_client_, DidNotHandleInputEvent(true));
  gesture_.type = WebInputEvent::GestureFlingCancel;
  input_handler_->HandleInputEvent(gesture_);

  VERIFY_AND_RESET_MOCKS();
  input_handler_->MainThreadHasStoppedFlinging();

  // Start a second gesture fling, this time in the +Y direction with no X.
  gesture_.type = WebInputEvent::GestureFlingStart;
  fling_delta = WebFloatPoint(0, -1000);
  fling_point = WebPoint(95, 87);
  fling_global_point = WebPoint(32, 71);
  modifiers = 2;
  gesture_.data.flingStart.velocityX = fling_delta.x;
  gesture_.data.flingStart.velocityY = fling_delta.y;
  gesture_.sourceDevice = WebGestureEvent::Touchpad;
  gesture_.x = fling_point.x;
  gesture_.y = fling_point.y;
  gesture_.globalX = fling_global_point.x;
  gesture_.globalY = fling_global_point.y;
  gesture_.modifiers = modifiers;
  EXPECT_CALL(mock_input_handler_, ScheduleAnimation());
  EXPECT_CALL(mock_input_handler_, ScrollBegin(testing::_, testing::_))
      .WillOnce(testing::Return(cc::InputHandler::ScrollStarted));
  EXPECT_CALL(mock_input_handler_, ScrollEnd());
  input_handler_->HandleInputEvent(gesture_);

  testing::Mock::VerifyAndClearExpectations(&mock_input_handler_);

  // Start the second fling animation at time 30.
  EXPECT_CALL(mock_input_handler_, ScheduleAnimation());
  EXPECT_CALL(mock_input_handler_, ScrollBegin(testing::_, testing::_))
      .Times(0);
  time = base::TimeTicks() + base::TimeDelta::FromSeconds(30);
  input_handler_->Animate(time);

  testing::Mock::VerifyAndClearExpectations(&mock_input_handler_);

  // Tick the second fling once normally.
  EXPECT_CALL(mock_input_handler_, ScheduleAnimation());
  EXPECT_CALL(mock_input_handler_, ScrollBegin(testing::_, testing::_))
      .WillOnce(testing::Return(cc::InputHandler::ScrollStarted));
  EXPECT_CALL(mock_input_handler_,
              ScrollBy(testing::_,
                       testing::Property(&gfx::Vector2dF::y, testing::Gt(0))))
      .WillOnce(testing::Return(true));
  EXPECT_CALL(mock_input_handler_, ScrollEnd());
  time += base::TimeDelta::FromMilliseconds(100);
  input_handler_->Animate(time);

  testing::Mock::VerifyAndClearExpectations(&mock_input_handler_);

  // Then abort the second fling.
  EXPECT_CALL(mock_input_handler_, ScheduleAnimation());
  EXPECT_CALL(mock_input_handler_, ScrollBegin(testing::_, testing::_))
      .WillOnce(testing::Return(cc::InputHandler::ScrollOnMainThread));
  EXPECT_CALL(mock_input_handler_, ScrollBy(testing::_, testing::_)).Times(0);
  EXPECT_CALL(mock_input_handler_, ScrollEnd()).Times(0);

  // We should get parameters from the second fling, nothing from the first
  // fling should "leak".
  EXPECT_CALL(
      mock_client_,
      TransferActiveWheelFlingAnimation(testing::AllOf(
          testing::Field(&WebActiveWheelFlingParameters::delta,
                         testing::Eq(fling_delta)),
          testing::Field(&WebActiveWheelFlingParameters::point,
                         testing::Eq(fling_point)),
          testing::Field(&WebActiveWheelFlingParameters::globalPoint,
                         testing::Eq(fling_global_point)),
          testing::Field(&WebActiveWheelFlingParameters::modifiers,
                         testing::Eq(modifiers)),
          testing::Field(&WebActiveWheelFlingParameters::startTime,
                         testing::Eq(30)),
          testing::Field(&WebActiveWheelFlingParameters::cumulativeScroll,
                         testing::Field(&WebSize::height, testing::Lt(0))))));
  time += base::TimeDelta::FromMilliseconds(100);
  input_handler_->Animate(time);
}

TEST_F(InputHandlerProxyTest, GestureFlingStartedTouchscreen) {
  // We shouldn't send any events to the widget for this gesture.
  expected_disposition_ = DidHandle;
  VERIFY_AND_RESET_MOCKS();

  EXPECT_CALL(mock_input_handler_, ScrollBegin(testing::_, testing::_))
      .WillOnce(testing::Return(cc::InputHandler::ScrollStarted));
  gesture_.type = WebInputEvent::GestureScrollBegin;
  gesture_.sourceDevice = WebGestureEvent::Touchscreen;
  input_handler_->HandleInputEvent(gesture_);

  VERIFY_AND_RESET_MOCKS();

  EXPECT_CALL(mock_input_handler_, FlingScrollBegin())
      .WillOnce(testing::Return(cc::InputHandler::ScrollStarted));
  EXPECT_CALL(mock_input_handler_, ScheduleAnimation());

  gesture_.type = WebInputEvent::GestureFlingStart;
  gesture_.data.flingStart.velocityX = 10;
  gesture_.sourceDevice = WebGestureEvent::Touchscreen;
  input_handler_->HandleInputEvent(gesture_);

  VERIFY_AND_RESET_MOCKS();

  EXPECT_CALL(mock_input_handler_, ScrollEnd());

  // Verify that a GestureFlingCancel during an animation cancels it.
  gesture_.type = WebInputEvent::GestureFlingCancel;
  gesture_.sourceDevice = WebGestureEvent::Touchscreen;
  input_handler_->HandleInputEvent(gesture_);
}

TEST_F(InputHandlerProxyTest, GestureFlingOnMainThreadTouchscreen) {
  // We should send all events to the widget for this gesture.
  expected_disposition_ = DidNotHandle;
  VERIFY_AND_RESET_MOCKS();

  EXPECT_CALL(mock_input_handler_, ScrollBegin(testing::_, testing::_))
      .WillOnce(testing::Return(cc::InputHandler::ScrollOnMainThread));

  gesture_.type = WebInputEvent::GestureScrollBegin;
  input_handler_->HandleInputEvent(gesture_);

  VERIFY_AND_RESET_MOCKS();

  EXPECT_CALL(mock_input_handler_, FlingScrollBegin()).Times(0);

  gesture_.type = WebInputEvent::GestureFlingStart;
  gesture_.sourceDevice = WebGestureEvent::Touchscreen;
  input_handler_->HandleInputEvent(gesture_);

  VERIFY_AND_RESET_MOCKS();

  // Even if we didn't start a fling ourselves, we still need to send the cancel
  // event to the widget.
  gesture_.type = WebInputEvent::GestureFlingCancel;
  gesture_.sourceDevice = WebGestureEvent::Touchscreen;
  input_handler_->HandleInputEvent(gesture_);
}

TEST_F(InputHandlerProxyTest, GestureFlingIgnoredTouchscreen) {
  expected_disposition_ = DidHandle;
  VERIFY_AND_RESET_MOCKS();

  EXPECT_CALL(mock_input_handler_, ScrollBegin(testing::_, testing::_))
      .WillOnce(testing::Return(cc::InputHandler::ScrollStarted));

  gesture_.type = WebInputEvent::GestureScrollBegin;
  gesture_.sourceDevice = WebGestureEvent::Touchscreen;
  input_handler_->HandleInputEvent(gesture_);

  expected_disposition_ = DropEvent;
  VERIFY_AND_RESET_MOCKS();

  EXPECT_CALL(mock_input_handler_, FlingScrollBegin())
      .WillOnce(testing::Return(cc::InputHandler::ScrollIgnored));

  gesture_.type = WebInputEvent::GestureFlingStart;
  gesture_.sourceDevice = WebGestureEvent::Touchscreen;
  input_handler_->HandleInputEvent(gesture_);

  VERIFY_AND_RESET_MOCKS();

  // Even if we didn't start a fling ourselves, we still need to send the cancel
  // event to the widget.
  gesture_.type = WebInputEvent::GestureFlingCancel;
  gesture_.sourceDevice = WebGestureEvent::Touchscreen;
  input_handler_->HandleInputEvent(gesture_);
}

TEST_F(InputHandlerProxyTest, GestureFlingAnimatesTouchscreen) {
  // We shouldn't send any events to the widget for this gesture.
  expected_disposition_ = DidHandle;
  VERIFY_AND_RESET_MOCKS();

  EXPECT_CALL(mock_input_handler_, ScrollBegin(testing::_, testing::_))
      .WillOnce(testing::Return(cc::InputHandler::ScrollStarted));

  gesture_.type = WebInputEvent::GestureScrollBegin;
  gesture_.sourceDevice = WebGestureEvent::Touchscreen;
  input_handler_->HandleInputEvent(gesture_);

  VERIFY_AND_RESET_MOCKS();

  // On the fling start, we should schedule an animation but not actually start
  // scrolling.
  gesture_.type = WebInputEvent::GestureFlingStart;
  WebFloatPoint fling_delta = WebFloatPoint(1000, 0);
  WebPoint fling_point = WebPoint(7, 13);
  WebPoint fling_global_point = WebPoint(17, 23);
  int modifiers = 7;
  gesture_.data.flingStart.velocityX = fling_delta.x;
  gesture_.data.flingStart.velocityY = fling_delta.y;
  gesture_.sourceDevice = WebGestureEvent::Touchscreen;
  gesture_.x = fling_point.x;
  gesture_.y = fling_point.y;
  gesture_.globalX = fling_global_point.x;
  gesture_.globalY = fling_global_point.y;
  gesture_.modifiers = modifiers;
  EXPECT_CALL(mock_input_handler_, ScheduleAnimation());
  EXPECT_CALL(mock_input_handler_, FlingScrollBegin())
      .WillOnce(testing::Return(cc::InputHandler::ScrollStarted));
  input_handler_->HandleInputEvent(gesture_);

  testing::Mock::VerifyAndClearExpectations(&mock_input_handler_);
  // The first animate call should let us pick up an animation start time, but
  // we shouldn't actually move anywhere just yet. The first frame after the
  // fling start will typically include the last scroll from the gesture that
  // lead to the scroll (either wheel or gesture scroll), so there should be no
  // visible hitch.
  EXPECT_CALL(mock_input_handler_, ScheduleAnimation());
  base::TimeTicks time = base::TimeTicks() + base::TimeDelta::FromSeconds(10);
  input_handler_->Animate(time);

  testing::Mock::VerifyAndClearExpectations(&mock_input_handler_);

  // The second call should start scrolling in the -X direction.
  EXPECT_CALL(mock_input_handler_, ScheduleAnimation());
  EXPECT_CALL(mock_input_handler_,
              ScrollBy(testing::_,
                       testing::Property(&gfx::Vector2dF::x, testing::Lt(0))))
      .WillOnce(testing::Return(true));
  time += base::TimeDelta::FromMilliseconds(100);
  input_handler_->Animate(time);

  testing::Mock::VerifyAndClearExpectations(&mock_input_handler_);

  EXPECT_CALL(mock_client_, DidHandleInputEvent());
  EXPECT_CALL(mock_input_handler_, ScrollEnd());
  gesture_.type = WebInputEvent::GestureFlingCancel;
  input_handler_->HandleInputEvent(gesture_);
}

TEST_F(InputHandlerProxyTest,
       GestureScrollOnImplThreadFlagClearedAfterFling) {
  // We shouldn't send any events to the widget for this gesture.
  expected_disposition_ = DidHandle;
  VERIFY_AND_RESET_MOCKS();

  EXPECT_CALL(mock_input_handler_, ScrollBegin(testing::_, testing::_))
      .WillOnce(testing::Return(cc::InputHandler::ScrollStarted));

  gesture_.type = WebInputEvent::GestureScrollBegin;
  input_handler_->HandleInputEvent(gesture_);

  // After sending a GestureScrollBegin, the member variable
  // |gesture_scroll_on_impl_thread_| should be true.
  EXPECT_TRUE(input_handler_->gesture_scroll_on_impl_thread_for_testing());

  expected_disposition_ = DidHandle;
  VERIFY_AND_RESET_MOCKS();

  // On the fling start, we should schedule an animation but not actually start
  // scrolling.
  gesture_.type = WebInputEvent::GestureFlingStart;
  WebFloatPoint fling_delta = WebFloatPoint(1000, 0);
  WebPoint fling_point = WebPoint(7, 13);
  WebPoint fling_global_point = WebPoint(17, 23);
  int modifiers = 7;
  gesture_.data.flingStart.velocityX = fling_delta.x;
  gesture_.data.flingStart.velocityY = fling_delta.y;
  gesture_.sourceDevice = WebGestureEvent::Touchscreen;
  gesture_.x = fling_point.x;
  gesture_.y = fling_point.y;
  gesture_.globalX = fling_global_point.x;
  gesture_.globalY = fling_global_point.y;
  gesture_.modifiers = modifiers;
  EXPECT_CALL(mock_input_handler_, ScheduleAnimation());
  EXPECT_CALL(mock_input_handler_, FlingScrollBegin())
      .WillOnce(testing::Return(cc::InputHandler::ScrollStarted));
  input_handler_->HandleInputEvent(gesture_);

  // |gesture_scroll_on_impl_thread_| should still be true after
  // a GestureFlingStart is sent.
  EXPECT_TRUE(input_handler_->gesture_scroll_on_impl_thread_for_testing());

  testing::Mock::VerifyAndClearExpectations(&mock_input_handler_);
  // The first animate call should let us pick up an animation start time, but
  // we shouldn't actually move anywhere just yet. The first frame after the
  // fling start will typically include the last scroll from the gesture that
  // lead to the scroll (either wheel or gesture scroll), so there should be no
  // visible hitch.
  EXPECT_CALL(mock_input_handler_, ScheduleAnimation());
  base::TimeTicks time = base::TimeTicks() + base::TimeDelta::FromSeconds(10);
  input_handler_->Animate(time);

  testing::Mock::VerifyAndClearExpectations(&mock_input_handler_);

  // The second call should start scrolling in the -X direction.
  EXPECT_CALL(mock_input_handler_, ScheduleAnimation());
  EXPECT_CALL(mock_input_handler_,
              ScrollBy(testing::_,
                       testing::Property(&gfx::Vector2dF::x, testing::Lt(0))))
      .WillOnce(testing::Return(true));
  time += base::TimeDelta::FromMilliseconds(100);
  input_handler_->Animate(time);

  testing::Mock::VerifyAndClearExpectations(&mock_input_handler_);

  EXPECT_CALL(mock_client_, DidHandleInputEvent());
  EXPECT_CALL(mock_input_handler_, ScrollEnd());
  gesture_.type = WebInputEvent::GestureFlingCancel;
  input_handler_->HandleInputEvent(gesture_);

  // |gesture_scroll_on_impl_thread_| should be false once
  // the fling has finished (note no GestureScrollEnd has been sent).
  EXPECT_TRUE(!input_handler_->gesture_scroll_on_impl_thread_for_testing());
}

TEST_F(InputHandlerProxyTest, LastInputEventForVSync) {
  expected_disposition_ = DropEvent;
  VERIFY_AND_RESET_MOCKS();

  gesture_.type = WebInputEvent::GestureFlingCancel;
  gesture_.timeStampSeconds = 1234;
  base::TimeTicks time =
      base::TimeTicks() +
      base::TimeDelta::FromSeconds(gesture_.timeStampSeconds);
  gesture_.modifiers |= WebInputEvent::IsLastInputEventForCurrentVSync;
  EXPECT_CALL(mock_input_handler_, DidReceiveLastInputEventForVSync(time));
  input_handler_->HandleInputEvent(gesture_);
}

}  // namespace

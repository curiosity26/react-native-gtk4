/**
 * Linux override of react-native/Libraries/Components/Button.
 *
 * Button.js styles itself only for iOS and Android, so on Linux it drew a
 * bare title. This one looks like a GTK (Adwaita) button: rounded, a light
 * neutral background with dark text, and hover and pressed states. `color`
 * is the background (like Android), with white text, as Adwaita's
 * suggested-action buttons; `disabled` dims it and stops presses. The
 * props are Button's; the title is not uppercased.
 *
 * @flow
 * @format
 */

import type {ButtonProps} from 'react-native-upstream/Libraries/Components/Button';

import Pressable from 'react-native-upstream/Libraries/Components/Pressable/Pressable';
import View from 'react-native-upstream/Libraries/Components/View/View';
import StyleSheet from 'react-native-upstream/Libraries/StyleSheet/StyleSheet';
import Text from 'react-native-upstream/Libraries/Text/Text';
import invariant from 'invariant';
import * as React from 'react';
import {useState} from 'react';

const Button = ({
  ref,
  ...props
}: {
  ref?: React.RefSetter<React.ElementRef<typeof Pressable>>,
  ...ButtonProps,
  nativeID?: ?string,
}): React.Node => {
  const {
    accessibilityLabel,
    accessibilityState,
    'aria-busy': ariaBusy,
    'aria-checked': ariaChecked,
    'aria-disabled': ariaDisabled,
    'aria-expanded': ariaExpanded,
    'aria-label': ariaLabel,
    'aria-selected': ariaSelected,
    importantForAccessibility,
    color,
    onPress,
    touchSoundDisabled,
    title,
    testID,
    nativeID,
    accessible,
    accessibilityActions,
    accessibilityHint,
    accessibilityLanguage,
    onAccessibilityAction,
  } = props;
  const [hovered, setHovered] = useState(false);

  let _accessibilityState = {
    busy: ariaBusy ?? accessibilityState?.busy,
    checked: ariaChecked ?? accessibilityState?.checked,
    disabled: ariaDisabled ?? accessibilityState?.disabled,
    expanded: ariaExpanded ?? accessibilityState?.expanded,
    selected: ariaSelected ?? accessibilityState?.selected,
  };
  const disabled =
    props.disabled != null ? props.disabled : _accessibilityState?.disabled;
  _accessibilityState =
    disabled !== _accessibilityState?.disabled
      ? {..._accessibilityState, disabled}
      : _accessibilityState;

  invariant(
    typeof title === 'string',
    'The title prop of a Button must be a string',
  );

  // As Button.js does for 'no': the title inside shouldn't get focus.
  const _importantForAccessibility =
    importantForAccessibility === 'no'
      ? 'no-hide-descendants'
      : importantForAccessibility;

  return (
    <Pressable
      accessible={accessible}
      accessibilityActions={accessibilityActions}
      onAccessibilityAction={onAccessibilityAction}
      accessibilityLabel={ariaLabel || accessibilityLabel}
      accessibilityHint={accessibilityHint}
      accessibilityLanguage={accessibilityLanguage}
      accessibilityRole="button"
      accessibilityState={_accessibilityState}
      importantForAccessibility={_importantForAccessibility}
      testID={testID}
      nativeID={nativeID}
      disabled={disabled}
      onPress={onPress}
      android_disableSound={touchSoundDisabled}
      onHoverIn={() => setHovered(true)}
      onHoverOut={() => setHovered(false)}
      ref={ref}
      style={[
        styles.button,
        color != null && {backgroundColor: color},
        disabled && styles.disabled,
      ]}>
      {({pressed}) => (
        <>
          {/* Adwaita shades a button for hover and press: darker for the
              neutral one, lighter (hover) or darker (press) on a color. */}
          {!disabled && (pressed || hovered) ? (
            <View
              style={[
                styles.shade,
                pressed
                  ? styles.pressedShade
                  : color != null
                    ? styles.hoveredColorShade
                    : styles.hoveredShade,
              ]}
            />
          ) : null}
          <Text
            style={[styles.text, color != null && styles.colorText]}
            disabled={disabled}>
            {title}
          </Text>
        </>
      )}
    </Pressable>
  );
};

Button.displayName = 'Button';

// Adwaita's light theme: buttons are 34px tall with 6px corners and a bold
// label; the neutral background is 10% black over the window, the accent
// is #3584E4 (pass it as `color` for a suggested-action look).
const styles = StyleSheet.create({
  button: {
    minHeight: 34,
    minWidth: 34,
    paddingHorizontal: 12,
    justifyContent: 'center',
    borderRadius: 6,
    backgroundColor: '#E6E6E6',
  },
  disabled: {opacity: 0.5},
  shade: {...StyleSheet.absoluteFill, borderRadius: 6},
  hoveredShade: {backgroundColor: 'rgba(0, 0, 0, 0.05)'},
  hoveredColorShade: {backgroundColor: 'rgba(255, 255, 255, 0.1)'},
  pressedShade: {backgroundColor: 'rgba(0, 0, 0, 0.2)'},
  text: {
    textAlign: 'center',
    color: 'rgba(0, 0, 0, 0.8)',
    fontSize: 14,
    fontWeight: 'bold',
  },
  colorText: {color: '#FFFFFF'},
});

export default Button;

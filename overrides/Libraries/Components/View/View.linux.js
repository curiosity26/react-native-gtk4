/**
 * Linux override of react-native/Libraries/Components/View/View (RN 0.87.1,
 * imports rewritten to react-native-upstream/...).
 *
 * Adds the `contextMenu` prop: menu items with onSelect callbacks, sent to
 * the host as plain items (js/menuItems.js); the host pops them up on
 * right-click, the Menu key and Shift+F10 and reports the choice
 * (onContextMenuSelect), which runs the item's onSelect. Also written as a
 * plain function taking `ref` (React 19) instead of Flow's component
 * syntax; otherwise unchanged.
 *
 * @flow strict-local
 * @format
 */

import type {HostInstance} from 'react-native-upstream/src/private/types/HostInstance';
import type {ViewProps} from 'react-native-upstream/Libraries/Components/View/ViewPropTypes';

import TextAncestorContext from 'react-native-upstream/Libraries/Text/TextAncestorContext';
import ViewNativeComponent from 'react-native-upstream/Libraries/Components/View/ViewNativeComponent';
import * as React from 'react';
import {use} from 'react';

import {contextMenuProps} from '../../../../js/menuItems';

export type ViewInstance = HostInstance;

function View({
  ref,
  ...props
}: {
  ref?: React.RefSetter<ViewInstance>,
  ...ViewProps,
}): React.Node {
  const hasTextAncestor = use(TextAncestorContext);

  const {
    accessibilityState,
    accessibilityValue,
    'aria-busy': ariaBusy,
    'aria-checked': ariaChecked,
    'aria-disabled': ariaDisabled,
    'aria-expanded': ariaExpanded,
    'aria-hidden': ariaHidden,
    'aria-label': ariaLabel,
    'aria-labelledby': ariaLabelledBy,
    'aria-live': ariaLive,
    'aria-selected': ariaSelected,
    'aria-valuemax': ariaValueMax,
    'aria-valuemin': ariaValueMin,
    'aria-valuenow': ariaValueNow,
    'aria-valuetext': ariaValueText,
    id,
    tabIndex,
    // $FlowFixMe[prop-missing] Linux: a context menu (js/menuItems.js).
    contextMenu,
    ...otherProps
  } = props;

  const resolvedProps = otherProps as {...ViewProps};

  const menuProps = contextMenuProps(
    contextMenu,
    // $FlowFixMe[prop-missing]
    otherProps.onContextMenuSelect,
  );
  if (menuProps != null) {
    // $FlowFixMe[prop-missing]
    resolvedProps.contextMenu = menuProps.contextMenu;
    // $FlowFixMe[prop-missing]
    resolvedProps.onContextMenuSelect = menuProps.onContextMenuSelect;
  }

  const parsedAriaLabelledBy = ariaLabelledBy?.split(/\s*,\s*/g);
  if (parsedAriaLabelledBy !== undefined) {
    resolvedProps.accessibilityLabelledBy = parsedAriaLabelledBy;
  }

  if (ariaLabel !== undefined) {
    resolvedProps.accessibilityLabel = ariaLabel;
  }

  if (ariaLive !== undefined) {
    resolvedProps.accessibilityLiveRegion =
      ariaLive === 'off' ? 'none' : ariaLive;
  }

  if (ariaHidden !== undefined) {
    resolvedProps.accessibilityElementsHidden = ariaHidden;
    if (ariaHidden === true) {
      resolvedProps.importantForAccessibility = 'no-hide-descendants';
    }
  }

  if (id !== undefined) {
    resolvedProps.nativeID = id;
  }

  if (tabIndex !== undefined) {
    resolvedProps.focusable = !tabIndex;
  }

  if (
    accessibilityState != null ||
    ariaBusy != null ||
    ariaChecked != null ||
    ariaDisabled != null ||
    ariaExpanded != null ||
    ariaSelected != null
  ) {
    resolvedProps.accessibilityState = {
      busy: ariaBusy ?? accessibilityState?.busy,
      checked: ariaChecked ?? accessibilityState?.checked,
      disabled: ariaDisabled ?? accessibilityState?.disabled,
      expanded: ariaExpanded ?? accessibilityState?.expanded,
      selected: ariaSelected ?? accessibilityState?.selected,
    };
  }

  if (
    accessibilityValue != null ||
    ariaValueMax != null ||
    ariaValueMin != null ||
    ariaValueNow != null ||
    ariaValueText != null
  ) {
    resolvedProps.accessibilityValue = {
      max: ariaValueMax ?? accessibilityValue?.max,
      min: ariaValueMin ?? accessibilityValue?.min,
      now: ariaValueNow ?? accessibilityValue?.now,
      text: ariaValueText ?? accessibilityValue?.text,
    };
  }

  const actualView =
    ref == null ? (
      <ViewNativeComponent {...resolvedProps} />
    ) : (
      <ViewNativeComponent {...resolvedProps} ref={ref} />
    );

  if (hasTextAncestor) {
    return (
      <TextAncestorContext value={false}>{actualView}</TextAncestorContext>
    );
  }
  return actualView;
}

View.displayName = 'View';

export default View;

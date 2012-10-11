<?php
/**
 * @file
 * Main drush command file for quick migrate.
 */

/**
 * Implements hook_drush_command().
 */
function provision_quickmigrate_drush_command() {
  $items = array();

  $items['provision-quickmigrate'] = array(
    'description' => 'Do a quick migration.',
    'arguments' => array(
      '@platform_name' => dt('The Drush alias of the platform.'),
      'new_uri' => dt("The new URI the side should reside on."),
    ),
    'examples' => array(
      'drush @site provision-migrate @platform_name' => 'Migrate the site as defined by the Drush alias, to the platform as defined by the platform\'s Drush alias',
    ),
    'bootstrap' => DRUSH_BOOTSTRAP_DRUPAL_ROOT,
  );
  return $items;
}

/**
 * Perform the actual migration.
 */
function drush_provision_quickmigrate($platform, $new_uri = NULL) {
  // Put the site under maintenance.
  drush_log(dt("Putting site under maintenance"));
  d('@self')->site_enabled = FALSE;
  _provision_drupal_create_settings_file();

  drush_set_option('old_platform', d()->platform->name);

  // If we are renaming the site, pick this up early.
  if (!is_null($new_uri) && ($new_uri != d()->uri)) {
    drush_set_option('target_name', $new_uri);
  }
  else {
    $new_uri = d()->uri;
    // the site url stays the same
    drush_set_option('target_name', '@' . d()->uri);
  }
  $target = drush_get_option('target_name');
  $new_path = d($platform)->root . "/sites/$new_uri";
  drush_set_option('new_site_path', $new_path);
  $old_path = d()->site_path;
  if (rename($old_path, $new_path)) {
    provision_quickmigrate_save_site(d(), $target, $new_uri);
    if (!drush_get_error()) {
      provision_backend_invoke($target, 'updatedb');
      provision_backend_invoke($target, 'cc all');
      // @todo This should be moved to hosting_quickmigrate.
      provision_backend_invoke('@hostmaster', 'hosting-import', array($target));
    }
  }
  if (!drush_get_error()) {
    drush_log(dt("Bringing site out of maintenance"));
    d()->site_enabled = true;
    drush_set_option('provision_save_config', false);
    provision_backend_invoke($target, 'provision-verify');
  }
}

/**
 * Save a site with provision_save().
 * @param $context
 *   The context to use.
 * @param $target
 *   The target platform.
 */
function provision_quickmigrate_save_site($context, $target, $new_uri = NULL) {
  $options = array();
  $options['uri'] = $context->uri;
  if (!is_null($new_uri) && ($new_uri != $context->uri)) {
    $options = $context->options;
    $options['uri'] = ltrim($new_uri, '@');
    $hash_name = drush_get_option('#name') ? '#name' : 'name';
    $options[$hash_name] = $new_uri;
  }
  $options['platform'] = $platform;
  $options['root'] = d($platform)->root;

  if ($profile = drush_get_option('profile', FALSE)) {
    $options['profile'] = $profile;
  }

  if ($db_server = drush_get_option('new_db_server', FALSE)) {
    $options['db_server'] = $db_server;
  }
  drush_backend_invoke_args('provision-save', array($target), $options);
}

/**
 * Handle rollback. If we have got to the point that we tried to move the site,
 * we need to move the site back and restore the database dump.
 */
function drush_provision_quickmigrate_rollback() {
  $new_path = drush_get_option('new_site_path');
  $old_path = d()->root;
  if (is_dir($new_path)) {
    if (rename($new_path, $old_path)) {
      provision_quickmigrate_save_site(d(), d()->platform, d()->uri);
      if (!drush_get_error()) {
        // @todo This should be moved to hosting_quickmigrate.
        provision_backend_invoke('@hostmaster', 'hosting-import', array($target));
      }
    }
    else {
      drush_set_error("Cannot rollback update");
    }
  }
}


/**
 * Fix bug in provision that doesn't put the site in maintenance mode.
 */
function provision_quickmigrate_provision_drupal_config($uri, $data) {
  if (!d()->site_enabled) {
    return array('$conf[\'maintenance_mode\'] = TRUE;');
  }
}
